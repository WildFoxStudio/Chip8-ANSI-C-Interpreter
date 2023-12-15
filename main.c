#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
//SDL2
#include <SDL2\SDL.h>
#include <SDL2\SDL_audio.h>

//DEBUG PRINT
#ifdef _DEBUG
#define p(...) printf(__VA_ARGS__);
#else
#define p(...)
#endif

#define true 1
#define false 0
#define bool int

//Super Chip-48, an interpreter for the HP48 calculator, added a 128x64-pixel mode. This mode is now supported by most of the interpreters on other platforms.
#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32
#define MEMORY_SIZE sizeof(uint8_t) * 0xFFF
#define V_REGISTER_SIZE sizeof(uint8_t) * 0x10
#define STACK_SIZE sizeof(uint16_t) * 0x10
#define KEY_SIZE sizeof(uint8_t) * 0x10
#define DISPLAY_SIZE sizeof(uint8_t) * DISPLAY_WIDTH * DISPLAY_HEIGHT
#define MAX_GAME_SIZE (0x1000 - 0x200)

//CHIP8 INTERNAL MEMORY RAPPRESENTATION
uint8_t MEMORY[0xFFF];//Main chip8 memory
uint8_t V[0x10];//16 general purpose 8-bit registers, usually referred to as Vx, where x is a hexadecimal digit (0 through F)
uint16_t I_REGISTER;//This is a 16-bit register called I. This register is generally used to store memory addresses, so only the lowest (rightmost) 12 bits are usually used.
uint16_t PC;//The program counter is used to store the currently executing address
uint16_t STACK[0x10];//The stack is an array of 16 16-bit values, used to store the address that the interpreter shoud return to when finished with a subroutine. Chip-8 allows for up to 16 levels of nested subroutines.
uint8_t SP;//The stack pointer it is used to point to the topmost level of the stack
uint8_t KEY[0x10];
uint8_t delay_timer;
uint8_t sound_timer;
uint8_t DISPLAY[DISPLAY_WIDTH][DISPLAY_HEIGHT];
uint16_t OPCODE;
uint8_t X;//A 4-bit value, the lower 4 bits of the high byte of the instruction
uint8_t Y;//A 4-bit value, the upper 4 bits of the low byte of the instruction
uint16_t NNN;//A 12-bit value, the lowest 12 bits of the instruction
uint8_t  KK;//lowest 8 bits
uint8_t  N;//lowest 4 bits
//Some flags
bool draw_flag;//update screen when is true
bool WAIT_KEY = false;//stall emulation and wait for a key press when is true

#define FONTSET_ADDRESS 0x00
#define FONTSET_BYTES_PER_CHAR 5

uint8_t chip8_fontset[80] = 
{ 
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F 
};

void LoadGame(char *filename) {
    FILE * file = fopen(filename, "rb");
    if (NULL == file) {
        SDL_ShowSimpleMessageBox(0, "Unable to open rom", filename, NULL);
        exit(42);
    }
    fread(&MEMORY[0x200], 1, MAX_GAME_SIZE, file);
    fclose(file);
};



typedef struct {uint32_t opcode; bool hasVariant; void(*func_ptr)();}INSTRUCTION_REF;
#define INSTRUCTIONS_COUNT 36
#define BIND_INSTRUCTION(o, hasvariant) {(uint32_t)o, hasvariant, chip8_func_##o}
#define CREATE_INSTRUCTION(o, code) void chip8_func_##o () code;                                       
//Implement instruction functions                   
CREATE_INSTRUCTION(0x00E0, {memset(DISPLAY, 0, DISPLAY_SIZE);draw_flag = true;          p("Clear screen\n");    })//clear 
CREATE_INSTRUCTION(0x00EE, {PC = STACK[--SP];                                           p("Return from subroutine PC(%i) = STACK[(%i)]; SP-1(%i)\n", PC, SP, SP);   })//return from subroutine (PC to address on top of the stack, then subtract 1 from the SP)
CREATE_INSTRUCTION(0x1000, {PC = NNN;                                                   p("Jump to nnn PC = %i\n", NNN);    })//jmp to nnn (set PC to nnn)
CREATE_INSTRUCTION(0x2000, {STACK[SP++] = PC;  PC = NNN;                                p("Call subroutine: SP+1(%i);STACK[%i]; PC(%i) = nnn(%i)\n", SP, SP, PC, NNN);  })//call subroutine from nnn (increment the SP, then puts the current PC on top of the stack. PC is set to nnn)
CREATE_INSTRUCTION(0x3000, {if(V[X] == KK) PC+=2;                                       p("Skip next instr if %i == %i\n", V[X], KK);       })//skip next instruction if(Vx == KK) PC+=2
CREATE_INSTRUCTION(0x4000, {if(V[X] != KK) PC+=2;                                       p("Skip next instr if %i != %i\n", V[X], KK);       })//skip next instruction if(Vx != KK) PC+=2
CREATE_INSTRUCTION(0x5000, {if(V[X] == V[Y]) PC+=2;                                     p("Skip next instr if %i == %i\n", V[X], V[Y]);     })//skip next instruction if(Vx == Vy) PC+=2
CREATE_INSTRUCTION(0x6000, {V[X] = KK;                                                  p("assign V[%i] = %i\n", X, KK);      })//Vx = KK
CREATE_INSTRUCTION(0x7000, {V[X] += KK;                                                 p("add V[%i] += %i\n", X, KK);      })//Vx += KK
//Aritmethic functions
CREATE_INSTRUCTION(0x8000, {V[X] = V[Y];                                                p("V[%i] = V[%i]\n", X, Y);     })//Vx = Vy
CREATE_INSTRUCTION(0x8001, {V[X] = V[X] | V[Y];                                         p("assign V[%i] = V[%i] OR V[%i]\n", X, X, Y);     })//Vx = Vx OR Vy
CREATE_INSTRUCTION(0x8002, {V[X] = V[X] & V[Y];                                         p("assign V[%i] = V[%i] AND V[%i]\n", X, X, Y);    })//Vx = Vx AND Vy
CREATE_INSTRUCTION(0x8003, {V[X] = V[X] ^ V[Y];                                         p("assign V[%i] = V[%i] XOR V[%i]\n", X, X, Y);    })//Vx = Vx XOR Vy
CREATE_INSTRUCTION(0x8004, {V[0xF] = ((int)V[X] + (int)V[Y] > 255)?1:0;V[X]=V[X]+V[Y];  p("add V[%i] += V[%i]\n", X, Y);   })//Vx += Vy set VF = carry -  If the result is greater than 8 bits (i.e., > 255,) VF is set to 1, otherwise 0. Only the lowest 8 bits of the result are kept, and stored in Vx.
CREATE_INSTRUCTION(0x8005, {V[0xF] = (V[X] > V[Y]) ? 1 : 0; V[X]=V[X]-V[Y];             p("sub V[%i] -= V[%i]\n", X, Y);    })//Vx -= Vy set VF = NOT borrow - If Vx > Vy, then VF is set to 1, otherwise 0. Then Vy is subtracted from Vx, and the results stored in Vx.
CREATE_INSTRUCTION(0x8006, {V[0xF] = V[X] & 0x1; V[X] = (V[X] >> 1);                    p("shr V[%i]>>1\n", X);     })//Vx >> 1 - If the least-significant bit of Vx is 1, then VF is set to 1, otherwise 0. Then Vx is divided by 2.
CREATE_INSTRUCTION(0x8007, {V[0xF] = (V[Y] > V[X]) ? 1 : 0; V[X] = V[Y] - V[X];         p("sub V[%i] = V[%i] - V[%i]\n", X, Y, X);  })//Vx = Vy - Vx - set VF = NOT borrow - If Vy > Vx, then VF is set to 1, otherwise 0. Then Vx is subtracted from Vy, and the results stored in Vx.
CREATE_INSTRUCTION(0x800E, {V[0xF] = (V[X] >> 7) & 0x1; V[X] = (V[X] << 1);             p("shl V[%i]<<1\n", X);     })//Vx << 1 - If the most-significant bit of Vx is 1, then VF is set to 1, otherwise to 0. Then Vx is multiplied by 2.
//Misc functions
CREATE_INSTRUCTION(0x9000, {if(V[X] != V[Y]) PC+=2;                                     p("Skip next instr if %i != %i\n", V[X], V[Y]);     })//skip next instruction if(Vx != Vy) PC+=2
CREATE_INSTRUCTION(0xA000, {I_REGISTER = NNN;                                           p("assign I = %i\n", NNN);     })//register I set to NNN
CREATE_INSTRUCTION(0xB000, {PC = NNN + V[0];                                            p("Jump to %i\n", PC);        })//jmp to location nnn+V0 - PC+=nnn+V[0] - The program counter is set to nnn plus the value of V0
CREATE_INSTRUCTION(0xC000, {V[X] = (rand() % 256) & KK;                                 p("random byte AND kk assign V[%i] = %i\n", X, V[X]);      })//Vx = random byte AND kk - generates a random number from 0 to 255, which is then ANDed with the value kk. The value is stored in Vx
CREATE_INSTRUCTION(0xD000, {
//Set collision flag to 0
V[0xF] = 0;
unsigned row = V[X];
unsigned col = V[Y];
unsigned byteI;
unsigned bitI;
for(byteI=0; byteI < N; byteI++)
{
    //Read n bytes from memory address at register I
    uint8_t byte = MEMORY[I_REGISTER + byteI];
    //Blit every bit of the byte to the screen
    for(bitI=0; bitI < 8; bitI++)
    {
    //Get the first bit
    uint8_t bit = (byte >> bitI) & 0x1;
    //Get the current bit on the screen
    unsigned px = row + (7 - bitI);
    unsigned py = col + byteI;

    if(px > DISPLAY_WIDTH) px -= DISPLAY_WIDTH;
    if(py > DISPLAY_HEIGHT) py -= DISPLAY_HEIGHT;

    uint8_t *pixelbit = &DISPLAY[px][py];
    //Set collision flag is pixel will be erased
    if(bit == 1 && *pixelbit == 1) V[0xF] = 1;
    //Blit the bit onto the screen buffer by XOR
    *pixelbit = *pixelbit ^ bit;
    }
}
draw_flag = true;                                                                   p("Draw\n");
})//Display n-byte starting at memory location I at (Vx, Vy), set VF = collision - reads n bytes from memory at the address I, display at (Vx, Vy). Sprites are XORed onto the screen, If this causes pixels to be erased, VF= 1 else VF=0 //sprite wrap around screen
CREATE_INSTRUCTION(0xE09E, {if(KEY[V[X]]) PC+=2;                                    p("Skip next inst if KEY[%i] is down\n", V[X]);})//Skip next instruction if key[Vx] is PRESSED PC+=2
CREATE_INSTRUCTION(0xE0A1, {if(!KEY[V[X]]) PC+=2;                                   p("Skip next inst if KEY[%i] is up\n", V[X]);})//Skip next instruction if key[Vx] is NOT PRESSED PC+=2
CREATE_INSTRUCTION(0xF007, {V[X] = delay_timer;                                     p("V[%i] = delta_timer(%i)\n", X, delay_timer);})//Vx = delay timer value
CREATE_INSTRUCTION(0xF00A, {WAIT_KEY = true;                                        p("Wait for a keypress\n");})//Wait for a key press store value of the key in Vx -All execution stops until a key is pressed, then the value of that key is stored in Vx.
CREATE_INSTRUCTION(0xF015, {delay_timer = V[X];                                     p("delta_timer = V[%i](%i)\n", X, V[X]);})//Delay timer = Vx
CREATE_INSTRUCTION(0xF018, {sound_timer = V[X];                                     p("sound_timer = V[%i](%i)\n", X, V[X]);})//Sound timer = Vx
CREATE_INSTRUCTION(0xF01E, {V[0xF] = (V[X] + I_REGISTER > 0xFFF)?1:0; I_REGISTER = I_REGISTER + V[X];   p("I += V[%i](%i)\n", X, V[X]);})//I += Vx; VF is set to 1 when there is a range overflow (I+VX>0xFFF), and to 0 when there isn't.
CREATE_INSTRUCTION(0xF029, {I_REGISTER = FONTSET_BYTES_PER_CHAR * V[X];             p("I = CharLocation(%i)\n", FONTSET_BYTES_PER_CHAR * V[X]);})//The value of I is set to the location for the hexadecimal sprite corresponding to the value of Vx
CREATE_INSTRUCTION(0xF033, {
    MEMORY[I_REGISTER]   = (V[X] % 1000) / 100; // hundred's digit
    MEMORY[I_REGISTER+1] = (V[X] % 100) / 10;   // ten's digit
    MEMORY[I_REGISTER+2] = (V[X] % 10);         // one's digit              
                                                                                    p("Store BCD representation of Vx\n");
})/*MEMORY the binary-coded decimal representation of VX, with the most significant of three digits at the address in I, the middle digit at I plus 1,
and the least significant digit at I plus 2. (In other words, take the decimal representation of VX, place the hundreds digit in memory at location in I,
the tens digit at location I+1, and the ones digit at location I+2.)
Store BCD representation of Vx in memory locations I, I+1, and I+2.
The interpreter takes the decimal value of Vx, and places the hundreds digit in memory at location in I, the tens digit at location I+1, and the ones digit at location I+2. */
CREATE_INSTRUCTION(0xF055, {
    unsigned i;
    for(i=0; i <= X; i++)
    {
        MEMORY[I_REGISTER + i] = V[i];
    }
    I_REGISTER += X+1;                                                              
                                                                                    p("Stores V0 to VX in memory\n");
})//Stores V0 to VX (including VX) in memory starting at address I. The offset from I is increased by 1 for each value written, but I itself is left unmodified.[d]
CREATE_INSTRUCTION(0xF065, {
       unsigned i;
    for(i=0; i <= X; i++)
    {
        V[i] = MEMORY[I_REGISTER + i];
    }
    I_REGISTER += X+1;                                                              
                                                                                    p("Fills V0 to VX from memory\n");
})//Fills V0 to VX (including VX) with values from memory starting at address I. The offset from I is increased by 1 for each value written, but I itself is left unmodified


//Fill the instruction set
INSTRUCTION_REF INSTRUCTION_SET[INSTRUCTIONS_COUNT] = {
    BIND_INSTRUCTION(0x00E0, 1),
    BIND_INSTRUCTION(0x00EE, 1),
    BIND_INSTRUCTION(0x1000, 0),
    BIND_INSTRUCTION(0x2000, 0),
    BIND_INSTRUCTION(0x3000, 0),
    BIND_INSTRUCTION(0x4000, 0),
    BIND_INSTRUCTION(0x5000, 0),
    BIND_INSTRUCTION(0x6000, 0),
    BIND_INSTRUCTION(0x7000, 0),
    BIND_INSTRUCTION(0x8000, 0),
    BIND_INSTRUCTION(0x8001, 1),
    BIND_INSTRUCTION(0x8002, 1),
    BIND_INSTRUCTION(0x8003, 1),
    BIND_INSTRUCTION(0x8004, 1),
    BIND_INSTRUCTION(0x8005, 1),
    BIND_INSTRUCTION(0x8006, 1),
    BIND_INSTRUCTION(0x8007, 1),
    BIND_INSTRUCTION(0x800E, 1),
    BIND_INSTRUCTION(0x9000, 0),
    BIND_INSTRUCTION(0xA000, 0),
    BIND_INSTRUCTION(0xB000, 0),
    BIND_INSTRUCTION(0xC000, 0),
    BIND_INSTRUCTION(0xD000, 0),
    BIND_INSTRUCTION(0xE09E, 1),
    BIND_INSTRUCTION(0xE0A1, 1),
    BIND_INSTRUCTION(0xF007, 1),
    BIND_INSTRUCTION(0xF00A, 1),
    BIND_INSTRUCTION(0xF015, 1),
    BIND_INSTRUCTION(0xF018, 1),
    BIND_INSTRUCTION(0xF01E, 1),
    BIND_INSTRUCTION(0xF029, 1),
    BIND_INSTRUCTION(0xF033, 1),
    BIND_INSTRUCTION(0xF055, 1),
    BIND_INSTRUCTION(0xF065, 1)
};


void Execute()
{
//Opcodes are 2 byte long and stored in big-endian
//Read 2 byte instruction from memory
OPCODE = MEMORY[PC] << 8 | MEMORY[PC+1];

//
X = (OPCODE >> 8) & 0x000F;//A 4-bit value, the lower 4 bits of the high byte of the instruction
Y = (OPCODE >> 4) & 0x000F;//A 4-bit value, the upper 4 bits of the low byte of the instruction
NNN = OPCODE & 0x0FFF;//A 12-bit value, the lowest 12 bits of the instruction
KK = OPCODE & 0x0FF;//lowest 8 bits
N = OPCODE & 0x0F;//lowest 4 bits

//Move to next instruction
PC += 2;

unsigned o;
for(o=0; o <= INSTRUCTIONS_COUNT; o++){
    if(o == INSTRUCTIONS_COUNT){
            printf("Unknown instruction for opcode 0x%04x\n", OPCODE);
            SDL_ShowSimpleMessageBox(0, "Unknown Opcode", "Unknown opcode", NULL);
            exit(-45);
        }
    //printf("Checking instruction for opcode 0x%04x\n", OPCODE);
    bool instruction_found = false;

    //printf("Checking instruction for opcode 0x%01x against 0x%01x\n", (OPCODE & 0xF000) , (INSTRUCTION_SET[o].opcode & 0xF000));
    if( (OPCODE & 0xF000) == (INSTRUCTION_SET[o].opcode & 0xF000) ){//check if instruction first byte correspond
        if(INSTRUCTION_SET[o].hasVariant) {//check if instruction has multiple definitions
            
            if(KK == (INSTRUCTION_SET[o].opcode & 0xFF))//check lowest 2 bytes 
                instruction_found = true;

            if(N == (INSTRUCTION_SET[o].opcode & 0xF))//check nibble if corresponds
                instruction_found = true;
        }   
        else instruction_found = true;//execute instruction if found
        
    if(instruction_found)
    {
    p("PC:%i  OPCODE 0x%04x\n",PC-2, OPCODE);
    INSTRUCTION_SET[o].func_ptr();//run instruction
    break;
    }
        
    }
}



//Subtract 1 every tick (60hz)
if(delay_timer > 0)
    delay_timer--;

if(sound_timer > 0)
    sound_timer--;

};


/* These are in charge of maintaining our sine function */
float sinPos;
float sinStep;
void populate_audio(void* data, Uint8 *stream, int len) {
	int i=0;
    float VOLUME = 127.0;
	for (i=0; i<len; i++) {
		/* Just fill the stream with sine! */
		stream[i] = (Uint8) (VOLUME * sinf(sinPos))+127.0f;
		sinPos += sinStep;
	}
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    
    AllocConsole();
    freopen("conin$", "r", stdin);
    freopen("conout$", "w", stdout);
    freopen("conout$", "w", stderr);
    printf("Debugging Window:\n");

    //Init SDL2 and a rendenrer
    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Window* m_win = SDL_CreateWindow("Chip8 Interpreter - written by Stanislav Kirichenko", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, 0u); 
    SDL_Renderer *m_display = SDL_CreateRenderer(m_win, -1, SDL_RENDERER_ACCELERATED);

    //Create a black texture as an output
    SDL_Texture *bitmapTex = SDL_CreateTexture(m_display, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    unsigned char* pixels = NULL;    int pitch = 0;
    if(SDL_LockTexture(bitmapTex, NULL, (void**)&pixels, &pitch ) < 0) {
        SDL_ShowSimpleMessageBox(0, "SDL failed to access texture", SDL_GetError(), NULL); 
        exit(-12);
    }
             
    memset(pixels, 0, pitch*DISPLAY_HEIGHT);//Fill black texture
    SDL_UnlockTexture(bitmapTex);

    SDL_Surface* formattedSurf = SDL_CreateRGBSurfaceWithFormat(0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 8, SDL_PIXELFORMAT_RGB888);


    //Create a sinewave generator
    SDL_AudioSpec audioSpec;
    /* Set up the requested settings */
	audioSpec.freq = 44100;
	audioSpec.format = AUDIO_U8;
	audioSpec.channels = 1;
	audioSpec.samples = 8192 ;
	audioSpec.callback = (*populate_audio);
	audioSpec.userdata = NULL;
    //init variables
    const float mpi = 3.1415926535897963f;
    sinPos = 0;    sinStep = 2.0f * mpi * 800.0f / 44100.0f;

    //Open audio device
    if (SDL_OpenAudio(&audioSpec, NULL) < 0){
        SDL_ShowSimpleMessageBox(0, "SDL Failed to open audio device", SDL_GetError(), NULL);   
        exit(-12);
    }
    
    ///Initialize chip---------------------------------------------------------------
    //Set from counter to 0x200
    PC          = 0x200;
    OPCODE      = 0;
    I_REGISTER  = 0;
    SP          = 0;
    draw_flag   = true;
    delay_timer = 0;
    sound_timer = 0;
    //Reset memory
    memset(MEMORY,      0, MEMORY_SIZE);
    memset(V,           0, V_REGISTER_SIZE);
    memset(STACK,       0, STACK_SIZE);
    memset(DISPLAY,     0, DISPLAY_SIZE);
    memset(KEY,         0, KEY_SIZE);
    //Copy font set into memory
    memcpy(MEMORY, chip8_fontset, FONTSET_BYTES_PER_CHAR * 16);


    //Load the game into memory
    if(argc > 1)
        LoadGame(argv[1]);
    else
    {
        SDL_ShowSimpleMessageBox(0, "Nothing to run", "Drag a rom on top of executable", NULL);
        exit(-1);
    }
    
 

    uint32_t rate = ((uint32_t) ((1.0f / 60.0f/*Hz*/) * 1000.0f));

    
    int running = 1;
    while(running)    {
        uint32_t time = SDL_GetTicks();
        SDL_Event e;
        while(SDL_PollEvent(&e) || WAIT_KEY)
        {
            if(e.type == SDL_QUIT)
                running = 0;

            //Execute 0xF00A (WAIT FOR KEY) INSTRUCTION
            if(WAIT_KEY && e.type == SDL_KEYDOWN)
            {
                switch(e.key.keysym.scancode){
                    case SDL_SCANCODE_1: V[X] = 0x0; WAIT_KEY = false; break;
                    case SDL_SCANCODE_2: V[X] = 0x1; WAIT_KEY = false; break;
                    case SDL_SCANCODE_3: V[X] = 0x2; WAIT_KEY = false; break;
                    case SDL_SCANCODE_4: V[X] = 0x3; WAIT_KEY = false; break;
                    case SDL_SCANCODE_Q: V[X] = 0x4; WAIT_KEY = false; break;
                    case SDL_SCANCODE_W: V[X] = 0x5; WAIT_KEY = false; break;
                    case SDL_SCANCODE_E: V[X] = 0x6; WAIT_KEY = false; break;
                    case SDL_SCANCODE_R: V[X] = 0x7; WAIT_KEY = false; break;
                    case SDL_SCANCODE_A: V[X] = 0x8; WAIT_KEY = false; break;
                    case SDL_SCANCODE_S: V[X] = 0x9; WAIT_KEY = false; break;
                    case SDL_SCANCODE_D: V[X] = 0xA; WAIT_KEY = false; break;
                    case SDL_SCANCODE_F: V[X] = 0xB; WAIT_KEY = false; break;
                    case SDL_SCANCODE_Z: V[X] = 0xC; WAIT_KEY = false; break;
                    case SDL_SCANCODE_X: V[X] = 0xD; WAIT_KEY = false; break;
                    case SDL_SCANCODE_C: V[X] = 0xE; WAIT_KEY = false; break;
                    case SDL_SCANCODE_V: V[X] = 0xF; WAIT_KEY = false; break;
                    default: break;
                }
            }
        }

        const uint8_t *key = SDL_GetKeyboardState(NULL);
        KEY[0x0] = key[SDL_SCANCODE_1]; 
        KEY[0x1] = key[SDL_SCANCODE_2];
        KEY[0x2] = key[SDL_SCANCODE_3];
        KEY[0x3] = key[SDL_SCANCODE_4];

        KEY[0x4] = key[SDL_SCANCODE_Q];
        KEY[0x5] = key[SDL_SCANCODE_W];
        KEY[0x6] = key[SDL_SCANCODE_E];
        KEY[0x7] = key[SDL_SCANCODE_R];

        KEY[0x8] = key[SDL_SCANCODE_A];
        KEY[0x9] = key[SDL_SCANCODE_S];
        KEY[0xA] = key[SDL_SCANCODE_D];
        KEY[0xB] = key[SDL_SCANCODE_F];

        KEY[0xC] = key[SDL_SCANCODE_Z];
        KEY[0xD] = key[SDL_SCANCODE_X];
        KEY[0xE] = key[SDL_SCANCODE_C];
        KEY[0xF] = key[SDL_SCANCODE_V];


        time = SDL_GetTicks();

        //Run the instruction
        Execute();

        if(sound_timer > 0){
            SDL_PauseAudio(0);
        }
        else{
            SDL_PauseAudio(1);
        }
        


        if(draw_flag)
        {
            //Update texture content
            SDL_LockTexture(bitmapTex, NULL, (void**)&pixels, &pitch );
            //Fill black texture
            
            unsigned k,j;
            for(j=0; j < DISPLAY_HEIGHT; j++)
            for(k=0; k < DISPLAY_WIDTH; k++)
            {
                uint8_t bit = DISPLAY[k][j] ? 255 : 0;
                memset(&pixels[j * pitch + k*4], bit, pitch);
                
            }
            SDL_UnlockTexture(bitmapTex);
            //set draw flag to false
            draw_flag = false;
        }
    
        SDL_RenderClear(m_display);
        SDL_RenderCopy(m_display, bitmapTex, NULL, NULL);
        SDL_RenderPresent(m_display);


        int32_t time_to_sleep = SDL_GetTicks() - time - rate;
        if(time_to_sleep < 0)
        {
        //printf("Sleep for %ims\n", time_to_sleep);
        SDL_Delay(abs(time_to_sleep));
        }
            
    
    }

    //Cleanup
    SDL_CloseAudio();
    SDL_DestroyTexture(bitmapTex);
    SDL_DestroyRenderer(m_display);
    SDL_DestroyWindow(m_win);
    SDL_Quit();
    return 0;
}
