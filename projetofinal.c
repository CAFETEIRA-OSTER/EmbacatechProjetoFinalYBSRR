#include <stdio.h> // Biblioteca para funções de entrada e saída padrão 
#include <stdlib.h> // Biblioteca para funções gerais 
#include <time.h> // Biblioteca para funções de tempo 
#include <stdbool.h> // Biblioteca para usar o tipo bool
#include "pico/cyw43_arch.h" // Biblioteca para o chip Wi-Fi CYW43
#include "pico/stdlib.h" // Biblioteca padrão do Raspberry Pi Pico
#include "hardware/gpio.h" // Biblioteca para controlar os GPIOs
#include "hardware/adc.h" // Biblioteca para o conversor analógico-digital (ADC)
#include "hardware/pio.h" // Biblioteca para o controlador PIO
#include "hardware/clocks.h" // Biblioteca para controlar os clocks
#include "ws2818b.pio.h" // Biblioteca para controlar os LEDs WS2812B
#include "hardware/pwm.h" // Biblioteca para o controlador PWM
#include "hardware/i2c.h" // Biblioteca para o controlador I2C
#include "inc/ssd1306.h" // Biblioteca para o display OLED SSD1306
#include "pico/multicore.h" // Biblioteca para usar o multicore do RP2040


#define LEDG_PIN 11 // Pino GPIO do LED verde
#define LEDB_PIN 12 // Pino GPIO do LED azul
#define LEDR_PIN 13 // Pino GPIO do LED vermelho
#define BTN_A 5 // Pino GPIO do botão A
#define BTN_B 6 // Pino GPIO do botão B
#define LED_COUNT 25 // Número de LEDs na matriz
#define LED_PIN 7 // Pino GPIO para controle da matriz de LEDs
#define DEBOUNCE_DELAY 50 // Atraso para debounce dos botões (em milissegundos)
#define BUZZER_PIN 21 // Pino GPIO do buzzer

const char *ssid = "."; // SSID da rede Wi-Fi
const char *password = "."; // Senha da rede Wi-Fi

const uint I2C_SDA = 14; // Pino GPIO SDA para o I2C
const uint I2C_SCL = 15; // Pino GPIO SCL para o I2C

// Variáveis globais
int BUZZER_FREQUENCY = 12000; // Frequência do buzzer (em Hz)
int tentativas = 0; // Contador de tentativas
int concatenacao = 0; // Variável para armazenar a concatenação dos números gerados
int digitos[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1}; // Array para armazenar os dígitos do padrão
int primeiro_digito = 0; // Variável para armazenar o primeiro dígito do padrão
int dificuldade_por_tempo = 200; // Variável para controlar a dificuldade do jogo (tempo de resposta)

struct pixel_t {
  uint8_t G, R, B; // Define uma estrutura para representar um pixel com componentes de cor verde, vermelho e azul
};
typedef struct pixel_t pixel_t; // Define um novo tipo de dado "pixel_t" como um alias para a estrutura pixel_t
typedef pixel_t npLED_t; // Define um novo tipo de dado "npLED_t" como um alias para a estrutura pixel_t

npLED_t leds[LED_COUNT]; // Cria um array chamado "leds" do tipo npLED_t (que representa os LEDs) com tamanho LED_COUNT

PIO np_pio; // Variável para armazenar a instância do PIO
uint sm; // Variável para armazenar o número da máquina de estado

void npInit(uint pin) { // Função para inicializar a matriz de LEDs
  uint offset = pio_add_program(pio0, &ws2818b_program); // Adiciona o programa PIO para controlar os LEDs WS2812B
  np_pio = pio0; // Define o PIO a ser usado (pio0)
  sm = pio_claim_unused_sm(np_pio, false); // Tenta alocar uma máquina de estado livre no PIO
  if (sm < 0) { // Se não houver máquina de estado livre no pio0, tenta no pio1
    np_pio = pio1;
    sm = pio_claim_unused_sm(np_pio, true);
  }
  ws2818b_program_init(np_pio, sm, offset, pin, 800000.f); // Inicializa o programa PIO com os parâmetros fornecidos
  for (uint i = 0; i < LED_COUNT; ++i) { // Inicializa todos os LEDs com a cor preta (desligados)
    leds[i].R = 0;
    leds[i].G = 0;
    leds[i].B = 0;
  }
}

void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) { // Função para definir a cor de um LED específico
  leds[index].R = r; // Define o componente vermelho do LED
  leds[index].G = g; // Define o componente verde do LED
  leds[index].B = b; // Define o componente azul do LED
}

void npClear() { // Função para apagar todos os LEDs (definir a cor como preto)
  for (uint i = 0; i < LED_COUNT; ++i)
    npSetLED(i, 0, 0, 0); // Define a cor do LED como preto (desligado)
}

void npWrite() { // Função para enviar os dados de cor para a matriz de LEDs
  for (uint i = 0; i < LED_COUNT; ++i) { // Envia os dados de cor para cada LED
    pio_sm_put_blocking(np_pio, sm, leds[i].G); // Envia o componente verde
    pio_sm_put_blocking(np_pio, sm, leds[i].R); // Envia o componente vermelho
    pio_sm_put_blocking(np_pio, sm, leds[i].B); // Envia o componente azul
  }
  sleep_us(100); // Aguarda um curto período para garantir que os dados sejam processados pelos LEDs
}

int debounce_button(int pin) {
    int estado_atual = gpio_get(pin);
    sleep_ms(DEBOUNCE_DELAY);
    int estado_final = gpio_get(pin);
    return estado_atual == estado_final ? estado_atual : -1;
}

int gerar_numero_aleatorio_1_a_4() {
    return (rand() % 4) + 1;
}

// Definição de uma função para inicializar o PWM no pino do buzzer
void pwm_init_buzzer(uint pin, int frequency) {  // Agora recebe a frequência
  gpio_set_function(pin, GPIO_FUNC_PWM);
  uint slice_num = pwm_gpio_to_slice_num(pin);

  pwm_config config = pwm_get_default_config();

  // Usa a frequência passada como parâmetro
  float clkdiv = (float)clock_get_hz(clk_sys) / (4096.0f * (float)frequency);
  if (clkdiv < 1.0f) {
      clkdiv = 1.0f;
  }
  uint8_t clkdiv_int = (uint8_t)clkdiv;
  uint8_t clkdiv_frac = (uint8_t)((clkdiv - clkdiv_int) * 256.0f);
  pwm_config_set_clkdiv_int_frac(&config, clkdiv_int, clkdiv_frac);

  pwm_init(slice_num, &config, true);
  pwm_set_gpio_level(pin, 0);
}

// Definição de uma função para emitir um beep com duração especificada
void beep(uint pin, uint duration_ms, int frequency) { // Recebe a frequência
  pwm_init_buzzer(pin, frequency); // Reconfigura o PWM com a nova frequência
  pwm_set_gpio_level(pin, 2048);
  sleep_ms(duration_ms);
  pwm_set_gpio_level(pin, 0);
  sleep_ms(100);
}

void gerar_padrao() {
    concatenacao = 0; // Reinicializa a concatenação
    for(int conjunto_numerico = 0; conjunto_numerico <= 8; conjunto_numerico++) {
        sleep_ms(100);
        int numero_aleatorio = gerar_numero_aleatorio_1_a_4();
        printf("%d", numero_aleatorio);

        if(concatenacao == 0) {
            concatenacao = numero_aleatorio;
        } else {
            concatenacao = concatenacao * 10 + numero_aleatorio;
        }
    }
}

void separar_padrao() {
    int numero = concatenacao;
    int total_digitos = 0;

    while (numero > 0 && total_digitos < 9) {
        digitos[total_digitos] = numero % 10;
        numero /= 10;
        total_digitos++;
    }

    for (int i = 0; i < total_digitos / 2; i++) {
        int temp = digitos[i];
        digitos[i] = digitos[total_digitos - i - 1];
        digitos[total_digitos - i - 1] = temp;
    }
}

int ler_posicao_joystick() {
    adc_select_input(0);
    uint16_t adc_x_raw = adc_read();
    adc_select_input(1);
    uint16_t adc_y_raw = adc_read();

    float x = (float)adc_x_raw / 4095.0;
    float y = (float)adc_y_raw / 4095.0;

    if (x < 0.3) { 
      npSetLED(2, 0, 0, 100);
      npWrite();
      sleep_ms(10);
      npClear();
      npWrite();
      sleep_ms(10);
      return 1; // BAIXO
    } else if (x > 0.7) {
        npSetLED(22, 0, 100, 100);
        npWrite();
        sleep_ms(10);
        npClear();
        npWrite();
        sleep_ms(10);
        return 4; // CIMA
    } else if (y < 0.3) {
        npSetLED(14, 100, 0, 0);
        npWrite();
        sleep_ms(10);
        npClear();
        npWrite();
        sleep_ms(10);
        return 3; // ESQUERDA
    } else if (y > 0.7) {
        npSetLED(10, 0, 100, 0);
        npWrite();
        sleep_ms(10);
        npClear();
        npWrite();
        sleep_ms(10);
        return 2; // DIREITA
    } else {
        return 0; // Centro (não usado para o jogo)
    }
}



// Função para configurar o PWM (chame uma vez no início do seu programa)
void setup_pwm(uint gpio_pin) {
    // Configura o pino como função PWM
    gpio_set_function(gpio_pin, GPIO_FUNC_PWM);

    // Obtém a "slice" PWM associada a este pino
    uint slice_num = pwm_gpio_to_slice_num(gpio_pin);

    // Configura um "wrap" (valor máximo do contador PWM)
    // Isso define a frequência.  Um valor menor aumenta a frequência.
    // Um bom valor inicial é 255 (para ter 256 níveis de brilho).
    pwm_set_wrap(slice_num, 255);

    // Inicia o PWM com duty cycle 0 (desligado)
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio_pin), 0);

    // Habilita o PWM na slice
    pwm_set_enabled(slice_num, true);
}

// Função para definir o brilho do LED (0-255)
void set_led_brightness(uint gpio_pin, uint16_t brightness) {
    // Obtém a slice PWM associada ao pino
    uint slice_num = pwm_gpio_to_slice_num(gpio_pin);

    // Garante que o brilho esteja dentro dos limites (0-255)
    if (brightness > 255) {
        brightness = 255;
    }

    // Define o nível do canal (duty cycle)
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio_pin), brightness);
}

void acerto_de_sequencia() {
  beep(BUZZER_PIN, 100, 120000); // Frequência alta para acerto
  set_led_brightness(LEDG_PIN, 12);
  sleep_ms(50);
  beep(BUZZER_PIN, 100, 120000); 
  set_led_brightness(LEDG_PIN, 0);
}

void erro_de_sequencia() {
  beep(BUZZER_PIN, 100, 2000);  // Frequência baixa para erro
  set_led_brightness(LEDR_PIN, 12);
  sleep_ms(50);
  beep(BUZZER_PIN, 100, 2000);  
  set_led_brightness(LEDR_PIN, 0);
}

void verificar_sequencia_por_joystick(int indice) {
  while(true) {


      int posicao_joystick = ler_posicao_joystick();
      if (digitos[indice] == posicao_joystick) {
          printf("Você acertou a sequência %d!\n", indice+1);
          tentativas++;
          break;
      } else if (posicao_joystick != 0) {
          printf("Você errou a sequência %d!\n", indice+1);
          erro_de_sequencia();
          tentativas = 0;
          return;
      }
      sleep_ms(20);
  }
  while (ler_posicao_joystick() != 0) {
      sleep_ms(20);
  }
}



void piscarLED(int digito) {
  if (digito == 1) {
    npSetLED(2, 0, 0, 100);
  } else if (digito == 2) {
    npSetLED(10, 0, 100, 0);
  } else if (digito == 3) {
    npSetLED(14, 100, 0, 0);
  } else if (digito == 4) {
    npSetLED(22, 0, 100, 100);
  }
  npWrite();
  sleep_ms(dificuldade_por_tempo);
  npClear();
  npWrite();
  sleep_ms(dificuldade_por_tempo);
}

void contagem_regressiva(){

  // Numero 3
  npSetLED(1, 100, 0, 0);
  npSetLED(2, 100, 0, 0);
  npSetLED(3, 100, 0, 0);
  npSetLED(8, 100, 0, 0);
  npSetLED(11, 100, 0, 0);
  npSetLED(12, 100, 0, 0);
  npSetLED(18, 100, 0, 0);
  npSetLED(21, 100, 0, 0);
  npSetLED(22, 100, 0, 0);
  npSetLED(23, 100, 0, 0);
  npWrite();
  sleep_ms(500);
  npClear();
  npWrite();
  sleep_ms(500);
  // Numero 2
  npSetLED(1, 100, 0, 0);
  npSetLED(2, 100, 0, 0);
  npSetLED(3, 100, 0, 0);
  npSetLED(6, 100, 0, 0);
  npSetLED(12, 100, 0, 0);
  npSetLED(18, 100, 0, 0);
  npSetLED(21, 100, 0, 0);
  npSetLED(22, 100, 0, 0);
  npSetLED(23, 100, 0, 0);
  npWrite();
  sleep_ms(500);
  npClear();
  npWrite();
  sleep_ms(500);
  // Numero 1
  npSetLED(1, 100, 0, 0);
  npSetLED(2, 100, 0, 0);
  npSetLED(3, 100, 0, 0);
  npSetLED(7, 100, 0, 0);
  npSetLED(12, 100, 0, 0);
  npSetLED(17, 100, 0, 0);
  npSetLED(16, 100, 0, 0);
  npSetLED(22, 100, 0, 0);
  npWrite();
  sleep_ms(500);
  npClear();
  npWrite();
  sleep_ms(500);

}

void primeira_tentativa() {
  uint64_t start_time = to_ms_since_boot(get_absolute_time()); // Captura o tempo inicial da tentativa
  int botao_correto = 0; // Variável para indicar se o botão correto foi pressionado
  primeiro_digito = digitos[0]; // Obtem o primeiro dígito da sequência

  set_led_brightness(LEDB_PIN, 12); // Acende o LED azul

  while (to_ms_since_boot(get_absolute_time()) - start_time < 5000) { // Loop por 5 segundos

    int posicao_joystick = ler_posicao_joystick(); // Lê a posição do joystick
    if (primeiro_digito == posicao_joystick) { // Verifica se a posição do joystick corresponde ao primeiro dígito da sequência
      botao_correto = 1; // Define a variável como 1 para indicar que o botão/sentido do joystick correto foi pressionado
    }
    if (posicao_joystick!= 0 && primeiro_digito!= posicao_joystick) { // Verifica se o joystick foi movido para uma posição incorreta
      tentativas = 0; // Reinicia o contador de tentativas
      printf("Você não pressionou o botão correto! \n"); // Exibe mensagem de erro
      erro_de_sequencia(); // Emite o sinal sonoro de erro
      set_led_brightness(LEDB_PIN, 0); // Apaga o LED azul
      break; // Sai do loop
    }
    if (botao_correto) { // Verifica se o botão/sentido do joystick correto foi pressionado
      printf("Você acertou! \n"); // Exibe mensagem de acerto
      set_led_brightness(LEDB_PIN, 0); // Apaga o LED azul
      tentativas++; // Incrementa o contador de tentativas
      break; // Sai do loop
    }
    sleep_ms(100); // Aguarda um curto período antes de verificar novamente
  }
  if (!botao_correto) { // Verifica se o botão/sentido do joystick correto não foi pressionado dentro do tempo limite
    printf("Tempo esgotado! \n"); // Exibe mensagem de tempo esgotado
    set_led_brightness(LEDB_PIN, 0); // Apaga o LED azul
    erro_de_sequencia(); // Emite o sinal sonoro de erro
  }
  while (ler_posicao_joystick()!= 0) { // Aguarda o joystick voltar para a posição central
    sleep_ms(100); // Aguarda um curto período antes de verificar novamente
  }
}

void display_message_callback() {

  // Preparar área de renderização para o display (ssd1306_width pixels por ssd1306_n_pages páginas)
  struct render_area frame_area = {
    start_column : 0,
    end_column : ssd1306_width - 1,
    start_page : 0,
    end_page : ssd1306_n_pages - 1
  };

  calculate_render_area_buffer_length(&frame_area); // Precisa dessa função para limpar a área do OLED

  // Zera o display inteiro
  uint8_t ssd[ssd1306_buffer_length];
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

  const char* messages[] = {"Iniciando", "Buscando WiFi", "oooooooooooo"};
  int y = 5;

  for (int j = 0; j < 3; j++) {
    const char* message = messages[j];
    for (int i = 0; message[i] != '\0'; i++) {
      ssd1306_draw_char(ssd, 5 + (i * 8), y, message[i]); // Assume que cada caractere tem 8 pixels de largura
      render_on_display(ssd, &frame_area);
      sleep_ms(j == 2 ? 200 : 50); // Ajuste o tempo de delay conforme necessário
    }
    y += (j == 1) ? 30 : 20; // Move para a próxima linha
  }
}

// inicia processo de multi-core
void start_display_message_task() {
  multicore_launch_core1(display_message_callback);
}


int main() {

  
  // Inicializa a comunicação serial para depuração
  stdio_init_all();

  
  // Inicialização do i2c
  i2c_init(i2c1, ssd1306_i2c_clock * 1000);
  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);

  // Processo de inicialização completo do OLED SSD1306
  ssd1306_init();



  start_display_message_task();

  if (cyw43_arch_init()) {
    printf("Falha ao inicializar o Wi-Fi\n");
    return -1;
  }

  cyw43_arch_enable_sta_mode();

  printf("Conectando à rede Wi-Fi %s...\n", ssid);

  if (cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 5000)) {
    printf("Falha na conexão Wi-Fi\n");

    // Display error message on OLED
    struct render_area frame_area = {
      start_column : 0,
      end_column : ssd1306_width - 1,
      start_page : 0,
      end_page : ssd1306_n_pages - 1
    };
    calculate_render_area_buffer_length(&frame_area);
    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
    const char* error_message = "Erro na conexao";
    for (int i = 0; error_message[i] != '\0'; i++) {
      ssd1306_draw_char(ssd, 5 + (i * 8), 5, error_message[i]);
    }
    render_on_display(ssd, &frame_area);
    
    printf("Não foi possível conectar ao Wi-Fi, continuando...\n");
  }

  sleep_ms(1000);


  // Para as mensagens do display
  multicore_reset_core1();

  

  // Limpa o display
  struct render_area frame_area = {
    start_column : 0,
    end_column : ssd1306_width - 1,
    start_page : 0,
    end_page : ssd1306_n_pages - 1
  };
  calculate_render_area_buffer_length(&frame_area);
  uint8_t ssd[ssd1306_buffer_length];
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

  // mostrastts Wi-Fi 
  const char* success_message = "WiFi Conectado";
  for (int i = 0; success_message[i] != '\0'; i++) {
    ssd1306_draw_char(ssd, 5 + (i * 8), 5, success_message[i]);
  }
  render_on_display(ssd, &frame_area);


  adc_init();
  adc_gpio_init(26);
  adc_gpio_init(27);

  gpio_init(LEDG_PIN);
  gpio_set_dir(LEDG_PIN, GPIO_OUT);
  setup_pwm(LEDG_PIN); 
  gpio_init(LEDB_PIN);
  gpio_set_dir(LEDB_PIN, GPIO_OUT);
  setup_pwm(LEDB_PIN); 
  gpio_init(LEDR_PIN);
  gpio_set_dir(LEDR_PIN, GPIO_OUT);
  setup_pwm(LEDR_PIN);
  gpio_init(BTN_B);
  gpio_set_dir(BTN_B, GPIO_IN);
  gpio_pull_up(BTN_B);
  gpio_init(BTN_A);
  gpio_set_dir(BTN_A, GPIO_IN);
  gpio_pull_up(BTN_A);  
  npInit(LED_PIN);

  // Configuração do GPIO para o buzzer como saída
  gpio_init(BUZZER_PIN);
  gpio_set_dir(BUZZER_PIN, GPIO_OUT);

  
  while (true) {
  
    // Preparar área de renderização para o display (ssd1306_width pixels por ssd1306_n_pages páginas)
    struct render_area frame_area = {
    start_column : 0,
    end_column : ssd1306_width - 1,
    start_page : 0,
    end_page : ssd1306_n_pages - 1
    };

    calculate_render_area_buffer_length(&frame_area); // Precisa dessa função para limpar a área do OLED

    // Zera o display inteiro
    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);

    const char* messages[] = {"Velocidade", "A adiciona 25", "B remove 25"};
    int y = 5;

    for (int j = 0; j < 3; j++) {
    const char* message = messages[j];
    for (int i = 0; message[i] != '\0'; i++) {
      ssd1306_draw_char(ssd, 5 + (i * 8), y, message[i]); // Assume que cada caractere tem 8 pixels de largura
      render_on_display(ssd, &frame_area);
      sleep_ms(5); 
    }
    y += (j == 1) ? 30 : 20; // Move para a proxima linha
    }

    sleep_ms(3500);

    while (true) {
    // Limpa o display
    memset(ssd, 0, ssd1306_buffer_length);

    // Mostra a dificuldade
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "Dificuldade");
    int len = strlen(buffer);
    int x = (ssd1306_width - (len * 8)) / 2; // Centraliza o texto
    for (int i = 0; buffer[i] != '\0'; i++) {
      ssd1306_draw_char(ssd, x + (i * 8), 5, buffer[i]);
    }

    snprintf(buffer, sizeof(buffer), "%d", dificuldade_por_tempo);
    len = strlen(buffer);
    x = (ssd1306_width - (len * 8)) / 2; // Centraliza o texto
    for (int i = 0; buffer[i] != '\0'; i++) {
      ssd1306_draw_char(ssd, x + (i * 8), 30, buffer[i]);
    }

    snprintf(buffer, sizeof(buffer), "Mova o joystick");
    len = strlen(buffer);
    x = (ssd1306_width - (len * 8)) / 2; // Centraliza o texto
    for (int i = 0; buffer[i] != '\0'; i++) {
      ssd1306_draw_char(ssd, x + (i * 8), 40, buffer[i]);
    }

    snprintf(buffer, sizeof(buffer), "Para iniciar");
    len = strlen(buffer);
    x = (ssd1306_width - (len * 8)) / 2; // Centraliza o texto
    for (int i = 0; buffer[i] != '\0'; i++) {
      ssd1306_draw_char(ssd, x + (i * 8), 50, buffer[i]);
    }

    render_on_display(ssd, &frame_area);

    if (gpio_get(BTN_A) == 0) {
      sleep_ms(50);
      if (gpio_get(BTN_A) == 0) {
      dificuldade_por_tempo += 25;
      }
    }

    if (gpio_get(BTN_B) == 0) {
      sleep_ms(50);
      if (gpio_get(BTN_B) == 0) {
      dificuldade_por_tempo -= 25;
      if (dificuldade_por_tempo < 25) {
      dificuldade_por_tempo = 25;
      }
      }
    }

    if (ler_posicao_joystick() != 0) {
      // Limpa o display
      memset(ssd, 0, ssd1306_buffer_length);
      render_on_display(ssd, &frame_area);
      break;
    }

    sleep_ms(30);

    
    }

      
    // Reinicializa a semente do gerador de números aleatórios
    srand(to_ms_since_boot(get_absolute_time()));

    // Gera um novo padrão de números aleatórios
    gerar_padrao();
    separar_padrao();

    int posicao_joystick = ler_posicao_joystick();

    if (tentativas == 0) { printf("\nInício \n"); contagem_regressiva(); piscarLED(digitos[0]); primeira_tentativa(); }
    if (tentativas == 1) { acerto_de_sequencia(); piscarLED(digitos[0]); piscarLED(digitos[1]); primeira_tentativa(); }
    if (tentativas == 2) { verificar_sequencia_por_joystick(1); }
    if (tentativas == 3) { acerto_de_sequencia(); piscarLED(digitos[0]); piscarLED(digitos[1]); piscarLED(digitos[2]); primeira_tentativa(); }
    if (tentativas == 4) { verificar_sequencia_por_joystick(1); } if (tentativas == 5) { verificar_sequencia_por_joystick(2); }
    if (tentativas == 6) { acerto_de_sequencia(); piscarLED(digitos[0]); piscarLED(digitos[1]); piscarLED(digitos[2]); piscarLED(digitos[3]); primeira_tentativa(); }
    if (tentativas == 7) { verificar_sequencia_por_joystick(1); } if (tentativas == 8) { verificar_sequencia_por_joystick(2); } if (tentativas == 9) { verificar_sequencia_por_joystick(3); }
    if (tentativas == 10) { acerto_de_sequencia(); piscarLED(digitos[0]); piscarLED(digitos[1]); piscarLED(digitos[2]); piscarLED(digitos[3]); piscarLED(digitos[4]); primeira_tentativa(); }
    if (tentativas == 11) { verificar_sequencia_por_joystick(1); } if (tentativas == 12) { verificar_sequencia_por_joystick(2); }
    if (tentativas == 13) { verificar_sequencia_por_joystick(3); } if (tentativas == 14) { verificar_sequencia_por_joystick(4); }
    if (tentativas == 15) { acerto_de_sequencia(); piscarLED(digitos[0]); piscarLED(digitos[1]); piscarLED(digitos[2]); piscarLED(digitos[3]); piscarLED(digitos[4]); piscarLED(digitos[5]); primeira_tentativa(); }
    if (tentativas == 16) { verificar_sequencia_por_joystick(1); } if (tentativas == 17) { verificar_sequencia_por_joystick(2); }
    if (tentativas == 18) { verificar_sequencia_por_joystick(3); } if (tentativas == 19) { verificar_sequencia_por_joystick(4); } if (tentativas == 20) { verificar_sequencia_por_joystick(5); }
    if (tentativas == 21) { acerto_de_sequencia(); piscarLED(digitos[0]); piscarLED(digitos[1]); piscarLED(digitos[2]); piscarLED(digitos[3]); piscarLED(digitos[4]); piscarLED(digitos[5]); piscarLED(digitos[6]); primeira_tentativa(); }
    if (tentativas == 22) { verificar_sequencia_por_joystick(1); } if (tentativas == 23) { verificar_sequencia_por_joystick(2); }
    if (tentativas == 24) { verificar_sequencia_por_joystick(3); } if (tentativas == 25) { verificar_sequencia_por_joystick(4); }
    if (tentativas == 26) { verificar_sequencia_por_joystick(5); } if (tentativas == 27) { verificar_sequencia_por_joystick(6); }
    if (tentativas == 28) { acerto_de_sequencia(); piscarLED(digitos[0]); piscarLED(digitos[1]); piscarLED(digitos[2]); piscarLED(digitos[3]); piscarLED(digitos[4]); piscarLED(digitos[5]); piscarLED(digitos[6]); piscarLED(digitos[7]); primeira_tentativa(); }
    if (tentativas == 29) { verificar_sequencia_por_joystick(1); } if (tentativas == 30) { verificar_sequencia_por_joystick(2); }
    if (tentativas == 31) { verificar_sequencia_por_joystick(3); } if (tentativas == 32) { verificar_sequencia_por_joystick(4); }
    if (tentativas == 33) { verificar_sequencia_por_joystick(5); } if (tentativas == 34) { verificar_sequencia_por_joystick(6); } if (tentativas == 35) { verificar_sequencia_por_joystick(7); }
    if (tentativas == 36) { acerto_de_sequencia(); piscarLED(digitos[0]); piscarLED(digitos[1]); piscarLED(digitos[2]); piscarLED(digitos[3]); piscarLED(digitos[4]); piscarLED(digitos[5]); piscarLED(digitos[6]); piscarLED(digitos[7]); piscarLED(digitos[8]); primeira_tentativa(); }
    if (tentativas == 37) { verificar_sequencia_por_joystick(1); } if (tentativas == 38) { verificar_sequencia_por_joystick(2); }
    if (tentativas == 39) { verificar_sequencia_por_joystick(3); } if (tentativas == 40) { verificar_sequencia_por_joystick(4); }
    if (tentativas == 41) { verificar_sequencia_por_joystick(5); } if (tentativas == 42) { verificar_sequencia_por_joystick(6); }
    if (tentativas == 43) { verificar_sequencia_por_joystick(7); } if (tentativas == 44) { verificar_sequencia_por_joystick(8); }
    if (tentativas == 45) {
      
    // Mensagem de parabens
    memset(ssd, 0, ssd1306_buffer_length);
    const char* congrats_message = "Parabens";
    int len = strlen(congrats_message);
    int x = (ssd1306_width - (len * 8)) / 2; // Cetraliza do texto
    for (int i = 0; congrats_message[i] != '\0'; i++) {
      ssd1306_draw_char(ssd, x + (i * 8), 30, congrats_message[i]);
    }
    render_on_display(ssd, &frame_area);
    sleep_ms(5000);
    
    tentativas = 0;
    }
  }
    
  sleep_ms(100);

  cyw43_arch_poll();

  cyw43_arch_deinit();

  return 0;
}