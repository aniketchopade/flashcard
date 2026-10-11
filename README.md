# flashcard

Flashcards for a CrowPanel ESP32 5.79" e-ink display (792x272), organized by subject and textbook section so new sections can be added one at a time.

## Layout

```
index.json              generated list of subjects and sections (do not edit by hand)
biology/4.5.json        one file per textbook section
biology/4.6.json
chemistry/2.1.json
tools/build_index.py    validates every section file and rebuilds index.json
firmware/flashcards/    Arduino sketch for the panel
```

## Section file

```json
{
  "subject": "biology",
  "section": "4.8",
  "title": "Mitochondria",
  "cards": [
    {"id": "bio-4.8-01", "q": "Question?", "a": "Short answer."}
  ]
}
```

- `subject` must match the folder name, `section` must match the file name.
- `title`: ASCII, up to 40 characters.
- `q`: ASCII, up to 120 characters. `a`: ASCII, up to 200 characters.
- `id`: unique across the repo, `<subject prefix>-<section>-<NN>` (`bio`, `chem`, `phys`, ...).

## Adding or changing cards

1. Add or edit `<subject>/<section>.json`.
2. Run `python3 tools/build_index.py` (fails with a list of problems if anything is invalid).
3. Commit both the section file and `index.json`, then push.

The `/flashcards` Claude skill does all three from photos of textbook pages.

## Panel

- First boot: choose a subject, then a section (or "All sections").
- The last choice is remembered across reboots.
- Study: MENU = show answer, EXIT = next card, dial press = back to the section list (re-downloads `index.json`, so newly pushed sections show up).
- Menus: dial up/down = move, dial press = select, EXIT = back.

Files are fetched from the GitHub API (`api.github.com/repos/aniketchopade/flashcard/contents/...`), which always returns the latest version.
