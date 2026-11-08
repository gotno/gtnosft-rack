# gotnosoft brand modules for [VCV Rack](https://vcvrack.com/)

### ctrl
<img src="ctrl.png" height="500" />

provides an [OSC](https://ccrma.stanford.edu/groups/osc/index.html)-based [API](API.md) for querying and controlling a VCV Rack patch. queries include listing modules in the rack and their connected cables, the structure of a module and its components (parameters, ports, lights, etc), and bitmap renders of modules, params, and ports as they appear in the rack. control includes adding and removing cables, and setting param values.

the context menu provides a direct way to render a module (as the library does, with params, ports, etc. intact), or the module's panel and overlays separately. renders will land as pngs in `gotnosoft_ctrl_render` in your rack user directory (Help -> Open user folder).

### Tx
<img src="tx.png" height="500" />

streams [opus](https://opus-codec.org/)-encoded audio over UDP. acts as an expander to ctrl, which manages the stream and provides an api.
