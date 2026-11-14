# route catalog

## receive routes (oscreceiver/oscrx)
BENCH
"/bench/reset"
"/bench/report"
"/bench/overlay_cache"
"/bench/prep_worker"
"/bench/adaptive_retry"
"/bench/back_pressure"
"/bench/heartbeat_direct"
"/bench/action_budget"
"/bench/supersede"
"/bench/chunk_pacing"

CONNECTION
"/register"
"/reset"
"/keepalive"

META
"/get/settings"
"/get/patch_info"

CABLES
"/get/cables"
"/add/cable"
"/remove/cable"

MODULES+PARAMS
"/get/modules"
"/get/module/structure"
"/get/module/state"
"/get/module/params/state"
"/set/param/value"

SUBSCRIPTIONS
"/subscribe/module/lights"

SYNTHETICS
"/act/click/param"
"/act/press/param"
"/act/release/param"

TEXTURES
"/get/texture"
"/ack/chunk"

UTIL
"/patch/open"

AUDIO
"/set/audio/enabled"
"/set/audio/preset"
"/set/audio/stats"


## send routes (oscsender/osctx)
BENCH
"/bench/report/begin"
"/bench/stat"
"/bench/counter"
"/bench/texture"
"/bench/report/end"
"/bench/reset/ack"
"/bench/chunk_pacing/ack"
"/bench/overlay_cache/ack"
"/bench/prep_worker/ack"
"/bench/adaptive_retry/ack"
"/bench/back_pressure/ack"
"/bench/heartbeat_direct/ack"
"/bench/action_budget/ack"
"/bench/supersede/ack"

CONNECTION
"/announce"
"/heartbeat"

META
"/set/settings"
"/set/patch_info"

CABLES
"/set/cable"
"/report/cable/count"
"/ack/add/cable"
"/ack/remove/cable"

MODULES+PARAMS
"/set/module/stub"
"/report/module/count"
"/set/module/structure"
"/set/module/structure/light"
"/set/module/structure/param"
"/set/module/structure/param/knob"
"/set/module/structure/param/slider"
"/set/module/structure/param/button"
"/set/module/structure/param/switch"
"/set/module/structure/port"
"/set/module/state"
"/set/param/state"
"/report/param/state/count"
"/ack/param/value"

SUBSCRIPTIONS
"/ack/subscribe/module/lights"
"/set/lights/state"

SYNTHETICS
"/ack/click/param"
"/ack/press/param"
"/ack/release/param"

TEXTURES
"/set/texture/chunk"

AUDIO
"/set/audio_stream"
"/ack/audio/enabled"
"/ack/audio/preset"
