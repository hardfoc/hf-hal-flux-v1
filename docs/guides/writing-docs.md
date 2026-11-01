# Writing these docs

How pages, figures and generated tables are made, so that the site stays true to the code.

## Preview the site

```bash
python3 -m pip install -r requirements-docs.txt
mkdocs serve
```

Open <http://127.0.0.1:8000>. `mkdocs build --strict` is what CI runs: a broken link, a missing image or an image
without alt text fails it.

## Write a page

- Say what the page is for in its first sentence, then answer it. Prefer short sections, tables and numbered steps.
- Link to code with a relative path, as GitHub resolves it (`../../lib/api/Foo.h`); the site rewrites the link to
  the file on GitHub at the commit that was built.
- Link to the API reference under `api/` (`../reference/api.md` explains it); the link is checked on the built
  site, after the API is merged.
- Use admonitions (`!!! note`, `!!! warning`) for what a reader must not miss, not for decoration.
- A new folder gets an `index.md`; `docs/.nav.yml` orders the top tabs.

## Add a figure

A figure is a Python module in `docs/diagrams/figures/`. It extracts its facts from the code in `data()`, lists the
files they come from in `"sources"`, and lays them out in `build(data)` with figkit (`tools/figkit/`). The module
docstring's `Reads:` paragraph is the figure's long description; the `alt` sentence is required.

```bash
python3 tools/figkit/build.py           # writes docs/diagrams/<id>.svg, <id>.dark.svg, figures.json
python3 tools/figkit/build.py --check   # what CI runs
```

Embed it on a page with a marker pair; the build fills it in:

```markdown
<!-- figure: docs-pipeline -->
<!-- /figure -->
```

The build refuses a figure whose text is below 4.5:1 contrast in either theme, whose content does not fit, or whose
cards overlap. Light and dark are the same layout, so they cannot drift apart.

## Add a generated table

A generator is a module in `tools/docs/generators/` with a `REGIONS` dict from a region name to a function that
returns Markdown. Mark the region on a page and run `python3 tools/docs/gen.py`; CI runs it with `--check`:

```markdown
<!-- generated: my-table -->
<!-- /generated -->
```

## Record a decision

Copy [the ADR template](../adr/template.md) to the next number in `docs/adr/` and add it to the table in
[Decisions](../adr/index.md).
