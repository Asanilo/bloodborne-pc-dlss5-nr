# Source and distribution boundary

The bbport root LICENSE remains GPL version 2. The separate Helper and forwarder
retain the upstream GPL version 3 license in [LICENSE.GPL-3.0](LICENSE.GPL-3.0).
File-level `GPL-3.0-or-later` and MIT SPDX declarations remain unchanged. Files
without a narrower declaration retain the upstream repository license. See
[migration.json](migration.json), the two source-provenance files and original
comments for the source/author record. No license has been upgraded or replaced.

GPLv2-only and GPLv3 code cannot simply be linked into one program. The Helper
therefore remains a separate executable and its forwarder a separate DLL loaded
only by that executable. Native does not link Helper objects, shader code or
NGX Windows headers. Publish corresponding Helper/forwarder source, build scripts,
modifications and license alongside their binaries, separately from Native.

There is an existing issue to retain for distribution review: the Native
`gpu/render/native_runtime_model.h` protocol export already carries an upstream
GPL-3.0-or-later declaration. This migration does not introduce that inclusion,
relicense it, or assert that separate processes alone settle its compatibility
with all GPLv2 Native code. Before a new combined public binary release, establish
applicable later-version permissions or obtain a compatible protocol-header
permission from its copyright holders; otherwise retain separate source/build
artifacts pending review. Do not remove the header's SPDX statement.

The FSF explains both [GPL version compatibility](https://www.gnu.org/licenses/quick-guide-gplv3.html)
and [separate programs versus a combined work](https://www.gnu.org/licenses/gpl-faq.en.html#MereAggregation).
Complex shared data semantics need consideration as well as process separation;
this document records the engineering boundary, not a blanket legal clearance.

NVIDIA DLLs/models/SDKs, driver libraries, GE/Wine installations and game assets
are not relicensed by this source import. They are external user-provided inputs.
Do not commit or include them in public source archives. Community modified NR
DLLs must be identified as modified, without claiming NVIDIA signature validity
or original-binary provenance.
