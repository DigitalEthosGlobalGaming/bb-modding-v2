# Initial publication provenance

Imported on 18 October 2026 from the local bbmod framework at
`modding/bloodborne_pc/third_party/bbmod`, maintained separately from the working
game installation. The containing port checkout uses Supermedo/bloodborne_pc;
this import starts a new repository history, not a fork of the entire game port.
Original file notices are retained.

Build output, caches, game content, settings, saves and packages are excluded.
Inspector is withheld because its generated Openborne/data tables identify
unresolved redistribution terms. The live Rust SDK also declared a `gameplay`
module whose source was absent at snapshot time; that declaration is excluded
until the implementation is ready. Other experimental host code is included
as source, without claiming gameplay validation. The other agent's original
working tree is unchanged. See the root ROADMAP.md for current acceptance goals.

Existing documentation contains reported earlier results; the current CI and
its logs establish checks for each published commit. The integration patch is
an inherited bbport 1.9 patch and must be revalidated for a particular port
revision before deployment. It does not include every later edit in the local
port checkout.
