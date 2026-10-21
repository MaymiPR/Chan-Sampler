"""Shared null-test code: FL project files (flp, build_project, vstchannel),
FL renders (flrender: FL's Sampler alone; nullfl: FL's Sampler and Chan
Sampler in one project), offline renders (nulltest), WAV files (wavio) and
paths. Importing it makes nulltest/ the working directory, so the scripts'
relative paths (renders_fl/, signals/, data/) work from anywhere."""
import os as _os
_os.chdir(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))))
