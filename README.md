# FTO

A light-hearted 4-player online co-op party game built in Unreal Engine 5. You and up to three friends run a city police
precinct for one shift and try to keep the city from sliding into chaos: answer calls, patrol, catch crimes in the act,
pull over drivers, chase getaway cars and book suspects, from a cat stuck in a tree to a bank heist.

Everything you see and hear is made in-house from code: the procedural city, the characters and their animations
(scripted in Blender), the vehicles, and even the sound effects (synthesised).

## Quick start
1. Install Unreal Engine 5.8, Visual Studio 2022/2026 with "Game development with C++", and Git LFS.
2. `git lfs install && git clone https://github.com/j1m5s3/FTO.git`
3. Open `FTO.uproject` (build when prompted) and press Play, or build and run from the command line (see docs).

To share with friends who don't have Unreal: `powershell -ExecutionPolicy Bypass -File Tools/Build/package.ps1`,
then zip `Build/Package/Windows`.

## Docs
- [Game design](docs/GameDesign.md)
- [Development: build, play with friends, controls, console commands, art & audio pipelines](docs/Development.md)
- [Credits & asset licences](docs/Credits.md)
