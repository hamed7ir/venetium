# venetium

venetium is chromium 150 running on old arm windows tablets. i made it because i
wanted a real modern browser on my surface rt and surface 2, and nothing new runs
on them anymore.

it is built on top of supermium (which brings modern chromium back to old
windows), but supermium is x86/x64 only, so the whole arm32 (armnt) port here is
my own work.
<img width="1280" height="720" alt="photo_2026-10-07_17-07-03" src="https://github.com/user-attachments/assets/22b96f5f-2fb0-4d95-b1eb-4c093b60b692" />


## what it runs on

- surface rt (tegra 3) and surface 2 (tegra 4), arm32
- windows 10 build 15035 (the arm build that boots on these tablets)
- graphics go through d3d9 / angle (gl es2), these gpus have no d3d11

## what works

- hardware h.264 video decode on the gpu over d3d9 (dxva / media foundation), so
  720p plays smooth on youtube and aparat
- gpu rasterization on tegra 4
- the renderer sandbox is on, on arm32 it uses a USER_LIMITED token so windows 10's
  parallel loader can still load the dlls
- vp9 / av1 still decode in software (the tegra chips have no hardware for them)

## how it is built

this repo is a patch series on top of chromium 150, plus the build tooling and a
device test kit. the patches are in `patches/` (0001–0043), the full build steps
are in `RECIPE.md`, and `VENETIUM-IDS.md` says what each patch does.

## status

it works on a real device: pages load, the sandbox is on, video plays. i am still
testing and tuning. next i want to add windows 8 / 8.1 (rt) support, jitless.

## credits

built on [supermium](https://github.com/win32ss/supermium) and ungoogled chromium. thanks to Jaybee form openRT community for porting CR77 and Sharing it, Thanks to osmium for the first try port of chromium for these devices.
And special thanks to kristibek, jimkoutso2008, Max RM, albert,guilherme,fraae, Peter and others who i may forget, all the community. 

it was possible with help of claude Ai.

<!-- add your own thanks here -->
