# ENet 1.3.18

Reliable UDP networking library by Lee Salzman, used by the multiplayer fork (`docs/multiplayer/PLAN.md`).
MIT licence: `LICENSE`.

- Source: https://github.com/lsalzman/enet, tag `v1.3.18` (ChangeLog: April 14, 2024).
- Archive: `https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.zip`, SHA-256
  `32d6b15611c2667b88cd865e0cb48c3c011ddefcff6dfbdb266b8e0b04fffab3`, downloaded 2026-10-13.
- Kept: the library sources (`*.c`), `include/enet/*.h`, `LICENSE`, `README` and `ChangeLog`, unmodified. Left
  out: upstream build files (CMake, autotools, premake, project files), `docs`, `m4` and `.github`.
- Local changes: none.

SHA-256 of the kept files:

```text
9809c36bbbf725ee072039590e50b3decbe09621563c21a8370eb522fc5c4944  ChangeLog
a29cd246638974da7c6edf423a0a69d4a6f7d759940b7cfa9a2bc3442ac3834c  LICENSE
30355fcc7dba245ab2462f17cd7ef9ba5eb141ef055cea36972480b66cd879eb  README
0d42c2a191c7d22ed0da2b14617b958cce2efd2a185b3aef9fe51a0fb6704ba2  callbacks.c
6af9da787bf5aaeb44befcf4e4a822d6e4d0972279bac73004f628bf06788774  compress.c
396e27689fe5ad6cc88d3a96fb93fa274f951663fe2f627391ab8f48a6d95830  host.c
20b3d8834bd19b77d9e8b6bcef4cd6fccb9875a4c1cdb0e5f7881b1b285c8d16  include/enet/callbacks.h
1780a6ecc27574a207438639869573f4e417bffc9c95f48a95837a887fad9acf  include/enet/enet.h
845120efb4ad1633eb2f61c8d8f5529b33de2170152779bc6046ac0453cab46b  include/enet/list.h
32de2a7e94e7dcfdf1b9fae2e4cde44f2e44ef49e9a1c4a9dac2165289da281f  include/enet/protocol.h
808d5c402c908c14afd0df385855a1b8713e5f69a04008c341a70928647d3e6e  include/enet/time.h
543482db06a0086983641f550effec827e0ccf722667d4e1956cc92cb074dca6  include/enet/types.h
c37171b9f5f37e813059f6e37f247d32ae96037b6f2165d743fc757c6ea80c83  include/enet/unix.h
5f9f3b9f5110d6a0e4be94bcf7950031985c278b49c922f637074e30784fc392  include/enet/utility.h
4d16a2c8301570fc54264bfe7bfb4b8bdbf86e2a1d8a6e047633e6fd79d4964f  include/enet/win32.h
c5350bfa7b7d3cc49fa4b2ebab92e359995e4b1ca47c2c17b0fa338dad87e5a4  list.c
cfa444c3bf8460a1a14909166b08efc2cf82112b4368011814396d6d09a3bce7  packet.c
a967be4d6b03e8059838587f6f0e13fe092109121f51bec4ed039721ddc1a157  peer.c
5f9bf575cef8c36a24687e4f4c2fd2f2d2f57232350816376a18f50b4a328095  protocol.c
eb901bc255150fdf867b4a71419b1b233e5bb0858abf298da282320a2fa02045  unix.c
bf36d2239b6fe5c779d84605c44f0fbeb48330a0a8ea26006a1af0c7ee7cb6f0  win32.c
```

The Windows build compiles `callbacks.c compress.c host.c list.c packet.c peer.c protocol.c win32.c` and links
`ws2_32` and `winmm`; `unix.c` is kept for completeness and not built.
