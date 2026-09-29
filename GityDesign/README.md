# Design notes

| File | What is in it |
|---|---|
| [`TOKENS.json`](./TOKENS.json) | Every colour and metric of the original design (Navy Dark). `tools/generate-tokens.py` turns it into `src/ui/theme/Tokens.h`; nothing else in the code writes a colour |
| [`THEMES.md`](./THEMES.md) | The seven themes, how each is built from the tokens, and the rules they all keep |
| [`SPEC.md`](./SPEC.md) | What goes where on screen, and how it looks |
| [`QT_MAPPING.md`](./QT_MAPPING.md) | How each part of the screen is built in Qt Widgets |

Product decisions and their reasons are in the root [`ARCHITECTURE.md`](../ARCHITECTURE.md).
