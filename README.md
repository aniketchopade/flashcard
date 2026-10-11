# flashcard

Flashcard deck for a CrowPanel ESP32 5.79" e-ink display (792x272).

The panel downloads `cards.json` from this repo and shows one card at a time:
MENU reveals the answer, rotary up/down = got it / missed it, EXIT = next card.

## Card format

```json
{
  "version": 1,
  "cards": [
    {"id": "bio-001", "deck": "biology", "chapter": "Cells",
     "q": "Which organelle makes most of the cell's ATP?",
     "a": "Mitochondria - site of cellular respiration"}
  ]
}
```

Rules: plain ASCII only, question <= 120 chars, answer <= 200 chars, one fact per card.
