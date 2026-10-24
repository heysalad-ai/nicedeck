// The implementation lives in Deck.cpp to keep Arduino's automatic prototypes
// separate from the configuration and input-state types.
void deckSetup();
void deckLoop();
void setup() { deckSetup(); }
void loop() { deckLoop(); }
