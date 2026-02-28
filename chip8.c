#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "SDL.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

typedef struct {
    uint32_t window_height; // Altura de la ventana
    uint32_t window_width; // Ancho de la ventana
    uint32_t fg_color; // Color de primer plano
    uint32_t bg_color; // Color de fondo
    uint32_t scale_factor; // Factor de escala
} config_t;

typedef enum {
    QUIT,
    RUNNING ,
    PAUSED,
} emulator_state_t;

typedef struct {
    uint16_t opcode;
    uint16_t NNN;
    uint8_t NN;
    uint8_t N;
    uint8_t X;
    uint8_t Y;
} instruction_t;

typedef struct {
    emulator_state_t state;
    uint8_t memory[4096];
    bool display[64 * 32];
    uint16_t stack[12];     //Stack de subrutinas
    uint8_t V[16];          //Registros V0 a VF
    uint16_t I;             //Registro de direcciones
    uint16_t PC;            //Contador de programa
    uint8_t delay_timer;
    uint8_t sound_timer;
    bool keypad[16];        //Teclado hexadecimal (0x0 a 0xF)
    char *rom_name;
    instruction_t inst;
} chip8_t;

bool init_sdl(sdl_t *sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("No se ha podido inicializar SDL: %s", SDL_GetError());
        return false;
    }

    //Crear ventana
    sdl->window = SDL_CreateWindow("Emulador CHIP-8",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   config.window_width * config.scale_factor, config.window_height * config.scale_factor,
                                   SDL_WINDOW_SHOWN);

    if (!sdl->window) {
        SDL_Log("No se ha podido crear la ventana: %s", SDL_GetError());
        return false;
    }

    //Crear renderer
    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);

    if (!sdl->renderer) {
        SDL_Log("No se ha podido crear el renderer: %s", SDL_GetError());
        return false;
    }

    return true;
}

bool init_chip8(chip8_t *chip8, char rom_name[]) {
    const uint32_t entry_point = 0x200; //Dirección de inicio del programa en memoria
    const uint8_t font[] = {
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

    //Cargar fuente en memoria
    memcpy(&chip8->memory[0], font, sizeof(font));

    //Cargar ROM en memoria
    FILE *rom = fopen(rom_name, "rb");
    if (!rom) {
        SDL_Log("El archivo que estás intentando abrir es inválido: %s", SDL_GetError());
        return false;
    }

    //Comprobar tamaño de la ROM
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_rom_size = sizeof(chip8->memory) - entry_point;
    rewind(rom);

    if (rom_size > max_rom_size) {
        SDL_Log("La ROM es demasiado grande para la memoria del CHIP-8: %s", SDL_GetError());
        fclose(rom);
        return false;
    }

    if (fread(&chip8->memory[entry_point], rom_size, 1, rom) != 1) {
        SDL_Log("Error al leer la ROM: %s", SDL_GetError());
        fclose(rom);
        return false;
    }

    fclose(rom);

    chip8->state = RUNNING;
    chip8->PC = entry_point;

    return true;
}

bool set_config_from_args(config_t *config, const int argc, char **argv){

    // Valores por defecto
    *config = (config_t){
        .window_height = 32, // Altura original de CHIP-8
        .window_width = 64, // Ancho original de CHIP-8
        .fg_color = 0xFFFF00FF, 
        .bg_color = 0x00000000,
        .scale_factor = 20,
    };

    // Sobreescribir valores por defecto con argumentos
    for (int i = 1; i < argc; i++) {
        (void)argv[i]; // Evitar advertencias de variables no utilizadas
    }

    return true;
}

void final_cleanup(const sdl_t sdl) {
    SDL_DestroyWindow(sdl.window);
    SDL_DestroyRenderer(sdl.renderer);
    SDL_Quit();
}

//Limpiar pantalla
void clear_screen(const sdl_t sdl, const config_t config){
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

void update_screen(const sdl_t sdl){
    SDL_RenderPresent(sdl.renderer);
}

void handle_user_input(chip8_t *chip8){
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                return;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        chip8->state = QUIT;
                        return;

                    case SDLK_SPACE:
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;
                            puts("=====PAUSADO=====");
                        } else if (chip8->state == PAUSED) {
                            chip8->state = RUNNING;
                            puts("=====REANUDADO=====");
                        }
                    default:
                        break;
                }
                break;

            case SDL_KEYUP:
                break;

            default:
                break;
        }
    }
}

// Emular 1 instrucción de CHIP-8
void emulate_instruction(chip8_t *chip8){
    // Conseguir siguiente instrucción (opcode) de la RAM
    chip8->inst.opcode = (chip8->memory[chip8->PC] << 8) | chip8->memory[chip8->PC + 1];
    // Pre-incrementar el PC para apuntar a la siguiente instrucción
    chip8->PC += 2;

    // Rellenar instrucciones
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x00FF;
    chip8->inst.N = chip8->inst.opcode & 0x000F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x000F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x000F;

    // Emular opcode
    switch ((chip8->inst.opcode >> 12) & 0x0F){
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                // 0x00E0: Limpiar pantalla
                memset(chip8->display[0], false, sizeof(chip8->display));
            } else if (chip8->inst.NN == 0xEE) {
                // 0x00EE: Retornar de subrutina
                
            }
            break;
        
        case 0x02:
            // 0x2NNN: Llamar a subrutina en NNN
            break;
            
        default:
            break;
    }
}

int main(int argc, char **argv) {
    // Comprobar argumentos
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <rom_name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Inicializar configuraciones
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

    //Inicializar SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, config)) exit(EXIT_FAILURE);

    //Inicializar emulador CHIP-8
    chip8_t chip8 = {0};
    char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

    //Limpiar pantalla
    clear_screen(sdl, config);

    //Bucle principal
    while (chip8.state != QUIT) {
        //Handle user input
        handle_user_input(&chip8);

        if (chip8.state == PAUSED) continue;

        //Get_time()

        //Instrucciones de emulación
        emulate_instruction(&chip8);

        //Get_time() elapsed since last get_time()

        // Delay 60hz +-
        SDL_Delay(16);

        //Actualizar ventana
        update_screen(sdl);
    }

    //Finalizar y limpiar procesos
    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
}