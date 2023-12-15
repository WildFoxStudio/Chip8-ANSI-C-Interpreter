clang -m32 main.c -I"D:\Libraries\include" -L"D:\Libraries\SDL_lib\x86" -lSDL2main -lSDL2 -Xlinker /subsystem:windows -o ".\x86\build.exe"
PAUSE