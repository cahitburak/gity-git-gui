# Themes

Choose one from **View ▸ Theme** or **Settings ▸ Appearance**. It applies immediately and is
remembered — for the credential prompt as well as the main window.

| Theme | Saved as | Window | Text | Accent | Selection |
|---|---|---|---|---|---|
| **Gity Dark** | `neutral-dark` | `#1E1E1E` | `#D0D0D0` | `#4A9EFF` | `#37393E` |
| **Gity Light** | `neutral-light` | `#FFFFFF` | `#262626` | `#0060C0` | `#D7E4F5` |
| **Navy Dark** | `dark` | `#1C1F26` | `#CBD3DE` | `#6AA9E0` | `#2C3646` |
| **Navy Light** | `light` | `#FAFBFC` | `#242A31` | `#1F5FA8` | `#DDE7F5` |
| **Midnight** | `midnight` | `#14161B` | `#CBD3DE` | `#6AA9E0` | `#222B39` |
| **Ember Steel Dark** | `ember-steel` | `#202123` | `#D0D3D8` | `#F0787E` | `#492A2E` |
| **Crimson Steel** | `crimson-steel` | `#111112` | `#CFCBCC` | `#FF5352` | `#5A0C10` |

**System** follows the desktop's light or dark setting with Navy Dark or Navy Light.

The saved names are older than the display names — the Navy pair was once called "Gity", and the
Gity pair plain "Dark" and "Light" — and were kept, so a saved choice keeps its look.

## How themes are built

`TOKENS.json` is Navy Dark, as designed. Every other theme is a list of overrides on top of it, in
`src/ui/theme/Theme.cpp`, stating only what it changes, and some stack: Gity Light, for example, is
Navy Light with the tint taken out.

| Theme | Built from |
|---|---|
| Navy Dark | `TOKENS.json` |
| Navy Light, Midnight | Navy Dark + their overrides |
| Gity Dark | Navy Dark + neutral greys |
| Gity Light | Navy Light + neutral greys |
| Ember Steel Dark, Crimson Steel | Gity Dark + their overrides |

The graph, the diff, the chips and the icons read the tokens when they paint, and the stylesheet
and palette are rebuilt from them, so a theme switch needs no restart. Light themes have their own
darker set of graph-lane colours — the same hues, legible on a light background.

## Rules every theme keeps

* **Text is at least 4.5:1** against the surface it sits on — body, secondary and selected text,
  diff gutters and hunk headers, and button text including on hover. Thin markers that are not
  text (the 2px selection bar) are held to 3:1.
* **Colour is never the only signal.** Every coloured state also carries a word or a shape.
* **Semantic colours keep their meaning:** green is added, red removed, amber a warning, purple a
  tag or rename. A theme's accent must not collide with them — which is why the red themes keep
  information silver and make removal salmon rather than crimson.

These are measured colour-pair contrasts, not a full accessibility audit.

## Adding a theme

1. Add a `Theme::Variant`, its saved name, its display name, and its place in `Theme::choices()`.
2. Write its overrides in `Theme.cpp` — start from the closest existing theme and layer on it.
3. Check every text colour against the surface it sits on, at 4.5:1.
4. If it is light, add it to `Theme::isLight` so the graph uses the light lane colours.

---

## Notes on the tinted themes

### Ember Steel Dark

Smoked graphite surfaces, cool silver text and controls, and a soft coral-red for selection, focus,
links and primary actions.

| Token | Value |
|---|---|
| Window · Panel · Header · Input | `#202123` · `#1B1C1E` · `#2B2D30` · `#161719` |
| Body · strong text | `#D0D3D8` · `#E1E4E8` |
| Accent · focus marker | `#F0787E` · `#E86B72` |
| Selection | `#492A2E` |
| Button · hover | `#96333D` · `#AD3C47` |

Information and modified-file labels stay silver. Removal and tag labels are brightened so their
chips stay readable on the red selection. Lowest measured text contrast: 5.14:1.

### Crimson Steel

Darker and redder than Ember Steel, with colours sampled from a carbon-and-crimson reference image:
a near-black ground, steel greys, blood-red and crimson.

| Token | Value |
|---|---|
| Window · Panel · Header · Input | `#111112` · `#0D0D0E` · `#1A1515` · `#0A0A0B` |
| Body · strong text | `#CFCBCC` · `#E6E2E3` — silver, from the reference's highlight `#B7B1B3` |
| Accent · focus marker | `#FF5352` · `#E82828` |
| Selection | `#5A0C10` — blood red |
| Button · hover | `#B81818` · `#D82828` |

The header's maroon-grey comes from the reference's perforated band. Information chips stay silver,
so red means "selected" or "do this", not every status; removal is salmon (`#F28B82`), so a deleted
line is never mistaken for the accent. Lowest measured text contrast: 4.94:1 (button text on hover);
the focus marker is 4.28:1.
