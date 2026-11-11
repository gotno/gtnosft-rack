# Module-specific considerations

## Blank

Blank is manually resizable, so its dimensions are instance state rather than
fixed properties of the module model.

- `ModuleStructureBundler` creates a fresh connected module/widget and immediately
  reads `box.size`. For Blank, this reports the default 10 HP width, not the
  resized width of an existing patch instance.
- `BlankWidget::step()` finishes layout by updating the panel size, screw
  positions and visibility, and the right resize-handle position. Stepping a
  temporary widget before parsing would capture initialized geometry, but would
  still only report the default width.
- Recommended approach: keep module structure as a default blueprint, initialize
  the temporary widget before inspecting it, and report the live widget's
  `box.size` through per-instance module state. `ModuleStateBundler` already
  accesses the live widget. The client can override blueprint dimensions with
  these values, allowing multiple Blanks with different widths.
- An alternative is to parse structure from a live widget by module ID. This
  captures instance-dependent geometry, but requires instance-specific structure
  identities and caching instead of treating plugin/module slugs as sufficient.
- Panel textures need corresponding handling: `Catalog::pullPanelId()` caches
  by plugin/module slug, and the renderer creates another default widget.
  Reporting actual dimensions alone therefore does not produce a matching
  resized panel texture. Stretching Blank's plain background may suffice if
  screws and handles are handled separately; faithful rendering needs
  size-aware or instance-specific texture generation.

Prefer the live widget's resolved geometry over special-casing Blank's serialized
`"width"` field, so the approach can also support other resizable modules.
