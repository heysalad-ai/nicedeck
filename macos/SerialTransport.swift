import Foundation
import Darwin

/// All file descriptor work stays on one queue. The UI never blocks on USB or a full CDC buffer.
final class SerialTransport {
    var onOpened: ((UUID) -> Void)?
    var onLine: ((UUID, Data) -> Void)?
    var onClosed: ((UUID, String?) -> Void)?
    private let queue = DispatchQueue(label: "NiceDeck.serial", qos: .userInitiated)
    private var descriptor: Int32 = -1
    private var session: UUID?
    private var timer: DispatchSourceTimer?
    private var incoming = Data()
    private var outgoing = Data()
    private var outgoingOffset = 0

    func open(path: String, session newSession: UUID) {
        queue.async { [weak self] in
            guard let self else { return }
            self.closeOnQueue(reason: nil, notify: false)
            self.session = newSession
            let fd = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
            guard fd >= 0 else {
                self.closeOnQueue(reason: "Could not open \(path): \(String(cString: strerror(errno)))", notify: true)
                return
            }
            self.descriptor = fd
            var settings = termios()
            guard tcgetattr(fd, &settings) == 0 else {
                self.closeOnQueue(reason: "Could not configure the USB serial port.", notify: true)
                return
            }
            cfmakeraw(&settings)
            cfsetispeed(&settings, speed_t(B115200))
            cfsetospeed(&settings, speed_t(B115200))
            settings.c_cflag |= tcflag_t(CLOCAL | CREAD)
            settings.c_cflag &= ~tcflag_t(CRTSCTS)
            guard tcsetattr(fd, TCSANOW, &settings) == 0 else {
                self.closeOnQueue(reason: "Could not set the USB serial speed.", notify: true)
                return
            }
            // CDC DTR tells the native ESP32 USB stack a host is listening.
            var modemBits: Int32 = TIOCM_DTR | TIOCM_RTS
            _ = ioctl(fd, TIOCMBIS, &modemBits)
            let timer = DispatchSource.makeTimerSource(queue: self.queue)
            timer.schedule(deadline: .now(), repeating: .milliseconds(15), leeway: .milliseconds(5))
            timer.setEventHandler { [weak self] in self?.pump() }
            self.timer = timer
            timer.resume()
            DispatchQueue.main.async { [weak self] in self?.onOpened?(newSession) }
        }
    }

    func send(_ data: Data, session expected: UUID) {
        queue.async { [weak self] in
            guard let self, self.session == expected, self.descriptor >= 0 else { return }
            guard self.outgoing.count - self.outgoingOffset + data.count <= 600000 else {
                self.closeOnQueue(reason: "The device is not accepting serial data.", notify: true)
                return
            }
            if self.outgoingOffset > 0 {
                self.outgoing.removeFirst(self.outgoingOffset)
                self.outgoingOffset = 0
            }
            self.outgoing.append(data)
            self.pump()
        }
    }

    func close() {
        queue.async { [weak self] in self?.closeOnQueue(reason: nil, notify: false) }
    }

    private func pump() {
        guard descriptor >= 0, let session else { return }
        var buffer = [UInt8](repeating: 0, count: 8192)
        // Bound each pump to keep disconnect/write work responsive even with noisy devices.
        for _ in 0..<48 {
            let count = Darwin.read(descriptor, &buffer, buffer.count)
            if count > 0 {
                incoming.append(contentsOf: buffer.prefix(count))
                if incoming.count > 600000 {
                    closeOnQueue(reason: "The device sent a serial message larger than the protocol permits.", notify: true)
                    return
                }
                while let newline = incoming.firstIndex(of: 10) {
                    var line = Data(incoming[..<newline])
                    incoming.removeSubrange(...newline)
                    if line.last == 13 { line.removeLast() }
                    if !line.isEmpty {
                        DispatchQueue.main.async { [weak self] in self?.onLine?(session, line) }
                    }
                }
            } else if count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR {
                closeOnQueue(reason: "USB connection lost.", notify: true)
                return
            } else { break }
        }
        for _ in 0..<16 where outgoingOffset < outgoing.count {
            let count = outgoing.withUnsafeBytes { bytes in
                Darwin.write(descriptor, bytes.baseAddress!.advanced(by: outgoingOffset), min(8192, outgoing.count - outgoingOffset))
            }
            if count > 0 { outgoingOffset += count }
            else if count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR {
                closeOnQueue(reason: "USB write failed.", notify: true)
                return
            } else { break }
        }
        if outgoingOffset == outgoing.count {
            outgoing.removeAll(keepingCapacity: true)
            outgoingOffset = 0
        }
    }

    private func closeOnQueue(reason: String?, notify: Bool) {
        let previous = session
        timer?.cancel()
        timer = nil
        if descriptor >= 0 { _ = Darwin.close(descriptor) }
        descriptor = -1
        session = nil
        incoming.removeAll(keepingCapacity: false)
        outgoing.removeAll(keepingCapacity: false)
        outgoingOffset = 0
        if notify, let previous {
            DispatchQueue.main.async { [weak self] in self?.onClosed?(previous, reason) }
        }
    }
}

/// Host actions run in arrival order, outside the serial loop. Disconnect cancels queued work.
final class HostActionExecutor {
    private let queue = DispatchQueue(label: "NiceDeck.actions", qos: .userInitiated)
    private let lock = NSLock()
    private var activeSession: UUID?
    var onError: ((String) -> Void)?

    func setSession(_ session: UUID?) {
        lock.lock()
        activeSession = session
        lock.unlock()
    }

    func enqueue(_ action: DeckAction, session: UUID, completion: @escaping (Bool, String?) -> Void) {
        guard action.type.isHostAction else { return }
        queue.async { [weak self] in
            guard let self else { return }
            self.lock.lock()
            let allowed = self.activeSession == session
            self.lock.unlock()
            guard allowed else {
                DispatchQueue.main.async { completion(false, "The connection changed.") }
                return
            }
            let process = Process()
            switch action.type {
            case .url:
                process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
                process.arguments = [action.value]
            case .app:
                process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
                process.arguments = ["-a", action.value]
            case .shell:
                process.executableURL = URL(fileURLWithPath: "/bin/zsh")
                process.arguments = ["-c", action.value]
            case .shortcut:
                process.executableURL = URL(fileURLWithPath: "/usr/bin/shortcuts")
                process.arguments = ["run", action.value]
            default: return
            }
            // A command that writes a lot cannot fill a pipe and deadlock the executor.
            process.standardInput = FileHandle.nullDevice
            process.standardOutput = FileHandle.nullDevice
            process.standardError = FileHandle.nullDevice
            do {
                try process.run()
                let deadline = Date().addingTimeInterval(25)
                while process.isRunning {
                    self.lock.lock()
                    let connected = self.activeSession == session
                    self.lock.unlock()
                    if !connected || Date() >= deadline {
                        process.terminate()
                        let message = connected ? "The host action exceeded 25 seconds." : "The connection changed."
                        DispatchQueue.main.async { completion(false, message) }
                        return
                    }
                    usleep(50000)
                }
                process.waitUntilExit()
                if process.terminationStatus != 0 {
                    let message = "\(action.type.title) finished with exit code \(process.terminationStatus)."
                    self.report(message)
                    DispatchQueue.main.async { completion(false, message) }
                } else { DispatchQueue.main.async { completion(true, nil) } }
            } catch {
                let message = "\(action.type.title) failed: \(error.localizedDescription)"
                self.report(message)
                DispatchQueue.main.async { completion(false, message) }
            }
        }
    }

    private func report(_ message: String) {
        DispatchQueue.main.async { [weak self] in self?.onError?(message) }
    }
}
