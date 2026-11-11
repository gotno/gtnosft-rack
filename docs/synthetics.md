# Synthetic UI Interactions in VCV Rack

A reference for synthesizing widget input without OS mouse input, revised using
the targeted parameter interactions implemented in gtnosft-rack.

## Choose the input model first

There are two different use cases:

| Goal | Approach |
|---|---|
| Reproduce a mouse click at a scene coordinate | Use `APP->event->handleButton()` and normal scene hit-testing |
| Interact with a specific parameter by ID, independently of the desktop pointer | Dispatch to that parameter and manage its lifecycle in a separate `EventState` |
| Set an ordinary parameter value | Use its `ParamQuantity`; do not synthesize input unnecessarily |
| Set a parameter whose module requires gesture callbacks | Use a targeted adapter for that plugin/model/parameter |

`handleButton()` is the normal mouse-input path, not a universally safe remote
control API. Using `APP->event` for a remote hold shares the real mouse's dragged
widget, selection, cursor behavior, and double-click tracking.

Run widget callbacks and synthetic-state transitions on the UI thread. Network
handlers should enqueue UI work rather than calling widgets themselves.

Examples use Rack namespaces for brevity. Plugin code should include `<rack.hpp>`;
individual internal header paths below identify the implementation being studied.

## Scene-coordinate input

Rack's widget tree is rooted at `app::Scene`. GLFW input is forwarded through
`APP->event`, a `widget::EventState`.

```cpp
bool EventState::handleButton(math::Vec pos, int button, int action, int mods);
bool EventState::handleHover(math::Vec pos, math::Vec mouseDelta);
```

`handleButton()` performs scene traversal, dispatches `ButtonEvent`, and manages
drag, selection, and double-click transitions. Release can also dispatch
`DragDropEvent` before ending the drag.

### Coordinates and target selection

Positions passed to these methods are in scene coordinates, measured in UI
pixels rather than raw framebuffer pixels. `Window::step()` derives scene size
from framebuffer size divided by pixel ratio.

```cpp
math::Vec center = param->getAbsoluteOffset(param->box.size.div(2.f));
```

`getAbsoluteOffset()` accounts for ancestor transforms, including Rack zoom and
scroll position. However, calculating a widget's center does not guarantee that
scene hit-testing reaches it:

- It may be outside a clipped viewport.
- It or an ancestor may be hidden.
- Another widget or overlay may receive the event first.
- A locked cursor suppresses the button hit-test in `handleButton()`.

The return value indicates whether a widget received the event, not whether the
intended module performed its application-specific action.

```cpp
APP->event->handleButton(center, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
// Release later, after allowing the module to observe the press.
APP->event->handleButton(center, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
```

Priming hover with `handleHover()` can trigger enter/leave behavior, but it is not
harmless if a drag already exists: it also delivers a drag-move event.

Scrolling or zooming to the target is an alternative when a genuine scene click
is desired. It is not a guaranteed targeted-dispatch method and changes the
user's view. Also, assigning `RackScrollWidget::offset` is not sufficient to
assume all child transforms are updated immediately; layout applies those
changes during widget stepping. Targeted remote controls need not manipulate
the view at all.

## Targeted parameter input

For a parameter resolved by ID, invoke its `onButton()` with a position in the
parameter's own coordinate system:

```cpp
widget::Widget* dispatchButton(app::ParamWidget* param, int action) {
    widget::EventContext context;
    widget::Widget::ButtonEvent event;
    event.context = &context;
    event.pos = param->box.size.div(2.f);
    event.button = GLFW_MOUSE_BUTTON_LEFT;
    event.action = action;
    event.mods = 0;
    param->onButton(event);
    return context.target;
}
```

This skips scene and ancestor hit-testing, allowing direct targeting even when
the parameter is hidden, covered, or offscreen. The parameter's own hit-test and
any child traversal it invokes still run. The handler may consume the event for
a child, so do not assume `context.target == param`.

Calling `onButton()` alone is incomplete. It does not automatically start a drag,
select a widget, dispatch a drop, end a drag, or track double-clicks. The caller
must deliberately implement the desired lifecycle.

### Separate synthetic event state

A separate `EventState` can manage drag/selection callbacks without occupying
the desktop mouse's state:

```cpp
widget::EventState synthetic; // Retain across a held interaction.

// Press.
widget::Widget* target = dispatchButton(param, GLFW_PRESS);
synthetic.setDraggedWidget(target, GLFW_MOUSE_BUTTON_LEFT);
synthetic.setSelectedWidget(target);

// Release later, while param and all retained targets remain valid.
widget::Widget* releasedOn = dispatchButton(param, GLFW_RELEASE);
synthetic.setDragHoveredWidget(nullptr);
if (releasedOn && synthetic.getDraggedWidget()) {
    widget::Widget::DragDropEvent event;
    event.button = GLFW_MOUSE_BUTTON_LEFT;
    event.origin = synthetic.getDraggedWidget();
    releasedOn->onDragDrop(event);
}
synthetic.setDraggedWidget(nullptr, 0);
synthetic.setSelectedWidget(nullptr);
```

This is the lifecycle skeleton, not a complete controller: it omits timing,
cursor suppression, validation, cancellation, and native-input arbitration.
The setters dispatch the corresponding virtual handlers, including
`onDragStart()` and `onDragEnd()`.

There is no need to assign a scene root or call `handleHover()` on this private
state. Deliver intentional drag moves directly to the chosen widget. Do not
feed desktop pointer movement into it.

Explicit targeted clicks need not synthesize double-clicks. This avoids
accidentally resetting knobs when two remote clicks occur close together.

### Cursor locking and physical mouse interference

A private `EventState` alone does not isolate every callback side effect:

- `Knob::onDragStart()` locks `APP->window`'s cursor in linear/scaled-linear mode.
- `Knob::onDragEnd()` unlocks it in those modes.
- Knob movement reads global knob settings and actual keyboard modifiers.
- `ParamWidget::onButton()` can still set Rack's `touchedParam`.
- Plugin overrides may query `APP->event` or other global UI state themselves.

The implemented adapter temporarily sets `settings::allowCursorLock = false`
around synthetic callbacks and restores it with RAII. This prevents those
callbacks from locking or unlocking the desktop cursor. It is a scoped global
setting, not a replacement for checking whether a native interaction is active.

Reject conflicting new interactions when the real pointer is dragging or the
cursor is already locked. If native input takes over the same target, relinquish
synthetic ownership without sending an extra release that would end the native
gesture.

Keeping a remote knob in `APP->event->draggedWidget` across UI frames is unsafe.
Rack polls cursor position every frame and forwards `DragMoveEvent` to that
widget. Physical movement can change its value or turn a hold into a drag,
even though the client sent no movement. The occupied drag slot also blocks
other code that correctly rejects competing pointer interactions.

## Press/release timing crosses threads

Dispatching both callbacks successfully does not prove that the engine observed
a click. Some modules sample a flag set by `onDragStart()` and cleared by
`onDragEnd()`. Back-to-back press/release in one UI action can occur entirely
between engine samples.

The default click implementation therefore:

1. Dispatches press and records the engine sample counter.
2. Retains the gesture and checks progress on subsequent UI ticks.
3. Releases after the counter has advanced by at least two samples.
4. Completes the request/ACK after release dispatch, not at initial acceptance.

The extra sample accounts for a sample already in progress when the UI changed
the flag: that sample might have checked it before the change. The counter
provides an opportunity for processing, not a module-specific acknowledgement.
A bypassed module or one that samples controls less frequently may need a
different policy. This also does not add synchronization to third-party flags
that a module already shares between UI and engine threads.

Apply the same minimum observation period to an explicit release received
immediately after a press. Serialize successive gestures with an engine-visible
released interval so a trigger can observe a new rising edge.

### Momentary switches have an additional release delay

`Switch::onDragEnd()` does not immediately lower a momentary parameter. It marks
the switch released; `Switch::step()` later resets the parameter to its minimum.
If it was also pressed that frame, the reset waits another widget step.

For these switches, wait for the parameter to return to its minimum before
starting the engine-visible released interval. Otherwise a second press can
arrive while the value is still high and merge two clicks into one.

If the engine stops, sample-count-based transitions wait rather than pretending
they completed. Do not block or sleep on the UI thread to force progress.

### Ownership and cleanup

A controller needs more than a press/release helper:

- Retain one synthetic hold and serialize requests that replace it.
- Release the previous hold before pressing a new target, including the same
  parameter again.
- Treat unmatched releases as no-ops; never release an unrelated native drag.
- Validate retained parameter and child-widget identities before dispatch.
- Clear ownership when widgets/modules disappear. A destruction guard may run
  after parameter children have died: forget pointers without calling them.
- Cancel queued completions when their target disappears.
- Release/cancel on disconnect or controller teardown, without retaining
  callbacks into a destroyed receiver.
- Preserve a released interval across reconnects; canceling callbacks must not
  let a new press merge with the previous session's hold.
- Protect deferred ACK session validation and enqueueing against concurrent
  session invalidation.

## Knob dragging and exact value writes

### Linear delta formula

For a vertical knob in ordinary linear mode:

```text
delta = -mouseDelta.y
      * settings::knobLinearSensitivity
      * knob->speed
      * modifierSpeed
      * rangeRatio
```

Horizontal knobs use `mouseDelta.x` instead. For bounded parameters:

```text
rangeRatio = pq->getRange()
           / ((knob->maxAngle - knob->minAngle) / (2 * pi))
```

For unbounded parameters, `rangeRatio = 1`, not an infinite parameter range.
With default sensitivity `0.001`, speed `1`, no modifiers, and range ratio `1`,
a value delta of `0.125` corresponds mathematically to 125 upward pixels.

This formula has important limits:

- Scaled-linear mode also changes a persistent scale from perpendicular motion.
- Rotary modes use cursor angle after drag-leave activation, unless
  `knob->forceLinear` is set.
- `forceLinear` does not itself disable cursor locking in start/end handlers.
- Modifier speed comes from `APP->window->getMods()`, not the original
  `ButtonEvent::mods`.
- Snapping accumulates fractional drag deltas within a gesture.
- Smoothing changes the engine target rather than immediately setting the value.
- Floating-point error, clamping, custom handlers, and intervening engine
  processing mean one large move is not generally equivalent to many small ones.

For a delta-based drag, calculate from the current quantity value. Using an
arbitrary saved `startValue` only reaches the requested target if the actual
starting value still matches it. Validate finite values and nonzero sensitivity.

### Case study: NoisePlethora `PROGRAM_PARAM`

`BefacoTinyKnobSnapPress` implements a multifunction unbounded encoder:

- Short press/release selects the active A/B section, not the next program.
- Holding longer than 0.5 seconds toggles program/bank mode.
- Rotation changes the program or bank in the selected mode.
- Its drag-start handler sets a held flag and clears its dragged flag.
- Cumulative movement greater than 5 pixels sets the dragged flag and cancels
  hold detection.
- The engine converts value differences to integer steps using resolution `8`.

The effective step size is `1 / 8 = 0.125`. However, simply synthesizing 125
vertical pixels per step proved insufficient as an exact-write strategy:
floating-point undershoot can fall just short of an integer conversion boundary.

Per-frame client updates introduce another issue. NoisePlethora updates its
reference value when it consumes a step, discarding overshoot. Restarting a drag
for every tiny client update can also reset gesture state. Raw small writes are
therefore not a substitute for tracking the client's requested position.

The implemented targeted override:

1. Registers step size `0.125` for `(Befaco, NoisePlethora, PROGRAM_PARAM)`.
2. Retains per-instance logical and applied values, accumulating fractional
   requests until a whole step can be applied.
3. For an actual step, reuses an existing synthetic hold on the same parameter
   or creates an isolated temporary drag.
4. Temporarily forces linear movement and disables smoothing.
5. Dispatches a horizontal `DragMoveEvent` of `(16, 0)`.
6. Assigns the exact quantized value with `pq->setImmediateValue()`.
7. Restores settings and ends only a temporary gesture, not a retained hold.

The horizontal activation exceeds NoisePlethora's 5-pixel threshold without
changing this vertical knob's value. Its distance also reaches Rack Knob's
16-pixel threshold, suppressing the click `ActionEvent` for a temporary drag.
This is a NoisePlethora-specific activation technique, not a generic encoder
rule. In scaled-linear mode perpendicular movement has additional internal
effects; the adapter assigns its intended value explicitly afterward.

An update that changes only the logical fraction dispatches no drag movement
and does not restart the hold timer. Actual encoder movement cancels hold
detection, matching native behavior. Synthetic holds must not be mistaken for
native drags when rebasing the accumulator.

Reported values expose the logical position. Native edits rebase it; module
destruction clears it. Step quantization uses floating-point boundary checks and
rejects nonfinite or excessive step counts rather than falling back to a raw
write that bypasses the module's gesture requirements.

The module leaves its dragged flag set after drag-end, allowing its engine
logic to consume a temporary write later. Do not generalize that behavior to
other modules without inspecting their handlers.

## Direct values, smoothing, display angles, and history

For parameters that do not require gesture side effects, prefer:

```cpp
pq->setValue(targetValue);
```

`setValue()` clamps, optionally snaps, and either sets the immediate value or
the smoothing target. In this Rack version:

| Method | Meaning |
|---|---|
| `getValue()` | Smoothing target if active, otherwise current value |
| `getImmediateValue()` | Current engine parameter value |
| `setImmediateValue()` | Set immediately, honoring clamping/snapping and canceling smoothing |

There is no `ParamQuantity::getSmoothValue()` method here.

Direct assignment does not synthesize button/drag callbacks, set `touchedParam`,
or automatically create an undo action. It cannot replace those callbacks for
modules like NoisePlethora.

`Knob::onDragEnd()` creates a `ParamChange` history entry only if the value
changed. It also emits `ActionEvent` when total traveled distance is below 16
pixels. Calling `setImmediateValue()` during a drag does not prevent that drag's
eventual history entry. Avoiding history requires a deliberate policy, not just
choosing a different setter.

For an ordinary bounded `SvgKnob` with nonzero range, display rotation follows a
linear rescale from parameter range to `[minAngle, maxAngle]`. Unbounded knobs
use `value * 2 * pi`; zero-range knobs use the midpoint angle. The displayed
angle wraps modulo a full turn, so visual rotation is not a unique inverse of
parameter value. Changing SVG angle alone also does not implement a module's
program-selection logic.

## Evidence and validation limits

The gtnosft-rack implementation is in:

- `src/osc/ParamInteraction.cpp`: targeted dispatch, private event state,
  engine-progress fences, request serialization, and cancellation.
- `src/osc/ParamWrite.hpp/.cpp`: override registry and logical/applied encoder
  state.
- `src/osc/OscReceiver.cpp`: UI-queued requests and deferred ACK completion.
- `src/osc/Bundler/ModuleCacheGuard.cpp`: module-lifetime cleanup.
- `tests/rack/test_param_interaction.cpp` and `test_param_encoder.cpp`: headless
  lifecycle/timing regressions, including a fixture reproducing NoisePlethora's
  engine-side gesture rules.

Headless tests can link `libRack.so` and exercise engine/widget classes without a
window, X server, or OpenGL context. They are not a full live-module GUI test.
Callbacks that query OS window state need either a live window or a narrowly
controlled test setup; a momentary-switch test alone does not cover knob cursor
locking or a custom module's audio-thread click detection.

## Relevant Rack and plugin source
note: these paths are in the Rack repository, not this one. check `RACK_DIR`, typically `../rack-git`

| File | Role |
|---|---|
| `include/widget/Widget.hpp` | Event structs, virtual handlers, traversal and hit-testing |
| `include/widget/event.hpp`, `src/widget/event.cpp` | Event state, setters, button/hover dispatch |
| `include/widget/OpaqueWidget.hpp` | Child traversal and left-button consumption |
| `src/widget/ZoomWidget.cpp` | Coordinate transforms |
| `src/app/RackScrollWidget.cpp` | Zoom, scroll offset, and layout |
| `src/window/Window.cpp` | Per-frame cursor polling, input dispatch, cursor lock/unlock |
| `src/app/ParamWidget.cpp` | Button handling, touched parameter, value-change events |
| `src/app/Knob.cpp` | Drag value calculation, cursor handling, Action and history |
| `src/app/Switch.cpp` | Momentary press/release and deferred widget-step reset |
| `src/app/SvgKnob.cpp` | Value-to-display-angle mapping |
| `src/engine/ParamQuantity.cpp` | Value setters, smoothing target/current reads |
| `src/engine/Engine.cpp` | Sample counter advances after module processing |
| `src/settings.cpp` | Knob mode and sensitivity defaults |
| `../plugins/Befaco/src/NoisePlethora.cpp` | Custom encoder callbacks and sampled hold/click logic |
