#include <msp430.h> 
#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "LiquidCrystal_I2C.h"

void configure_uart();
void cs();
void configure_adc();
void configurare_timer();
void init_I2C();
void I2C_Send (int value);
void pulseEnable (int value);
void write4bits (int value);
void LCD_Send(int value, int mode);
void LCD_Write (char *text);
void LCD_WriteNum(unsigned int num);
void LCD_SetCursor(int col, int row);
void LCD_ClearDisplay(void);
void LCD_Setup(void);
void LCD_leftToRight(void);
void LCD_rightToLeft(void);

volatile int i = 0;
volatile int adcvalue = 0;
char adcvalue_str[20];  // Buffer to hold the string. Make sure it's large enough.
bool adcdone = false;

const float Vin = 5.0; // The power supply value in V
const float R0 = 3.5; //R0 este RS/9.81

const float m = -0.33266; // calculated from mq2 datasheet
const float b = 1.475366; // calculated from mq2 datasheet

volatile float Vout = 0;
volatile float RS = 0 ;
volatile float ppm = 0 ;

char final[2] = "13";
char final2[2] = "10";

unsigned int TXBUF;
/**
 * main.c
 */
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer

    P5DIR |= BIT0;  // buzzer
    P5SEL1 &=~ BIT0;
    P5SEL0 |= BIT0;

    configure_uart();
    configurare_timer();
    configure_adc();
    cs();
    init_I2C();

    PM5CTL0 &= ~LOCKLPM5;

    __enable_interrupt();

    LCD_Setup();

    LCD_SetCursor(0, 0); // Initial position for the cursor at row 1, column 1.


    while(1){
        if(adcdone == true){
          LCD_ClearDisplay();
          Vout = adcvalue * (Vin / 4096.0);

          RS = (Vin - Vout) / Vout;

          ppm = pow(10, (log10(RS/R0) - b) / m);

          int ppm_intPart = (int)ppm;
          int ppm_fracPart = (int)((ppm - ppm_intPart) * 100);  // Keep two decimal places

          sprintf(adcvalue_str, "%d",ppm_intPart);

          for(i = 0; i < (strlen(adcvalue_str)); i++)
          {
              while (!(UCA1IFG&UCTXIFG)); // Wait for TX buffer to be ready
              UCA1TXBUF = adcvalue_str[i];
          }

          while (!(UCA1IFG&UCTXIFG)); // Wait for TX buffer to be ready
          UCA1TXBUF = '\n';

          while (!(UCA1IFG&UCTXIFG)); // Wait for TX buffer to be ready
          UCA1TXBUF = '\r';

          adcdone = false;

        }
        if (ppm > 400) {
            LCD_SetCursor(0, 0);
            LCD_Write("  S-au depasit   ");
            LCD_SetCursor(0, 1);
            LCD_Write("    400 ppm    ");
            for(i=0;i<2;i++){
                TB2CCR1 = 800;
                __delay_cycles(10000);

                TB2CCR1 = 900;
                __delay_cycles(5000);
            }
        } else {
            TB2CCR1 = 0; //buzzer off
            LCD_SetCursor(0, 0);
            LCD_Write("CO Concentration");
            LCD_SetCursor(0, 1);
            LCD_WriteNum(ppm);
            LCD_Write(" ppm      ");
        }
    }
}

void cs(){
    __bis_SR_register(SCG0); // disable FLL
    CSCTL3 |= SELREF__REFOCLK; // Set REFO as FLL reference source
    CSCTL0 = 0; // clear DCO and MOD registers
    CSCTL1 |= DCORSEL_3; // Set DCO = 8MHz
    CSCTL2 = FLLD_0 + 243; // DCOCLKDIV = 8MHz
    __delay_cycles(3);
    __bic_SR_register(SCG0); // enable FLL
    while(CSCTL7 & (FLLUNLOCK0 | FLLUNLOCK1)); // FLL locked
    CSCTL4 = SELMS__DCOCLKDIV | SELA__REFOCLK; // set default REFO(~32768Hz) as ACLK source, ACLK = 32768Hz
    CSCTL5 = DIVM_3 | DIVS_0 ; //MCLK=1MHz, SMCLK=1MHz
}

void configure_uart() // UCA1
{

    P4SEL0 |= BIT2 | BIT3; // UART 0: 6-> RX; 7-> TX

    // Configure UART MSP430FR2355 userguide page 586
    UCA1CTLW0 |= UCSWRST ;              // **Put state machine in reset**
    UCA1CTLW0 |= UCSSEL__SMCLK;         // SMCLK
    UCA1BRW = 8;                        // (1MHz/115200)
    UCA1MCTLW = 0xD600;                 // Modulation (UCBRSx, UCBRFx, UCOS16)
    UCA1CTLW0 &=~ UCSWRST;              // Reset
    UCA1IE |= UCRXIE;                   // Enable USCI_A1 RX interruptg
}

#pragma vector=USCI_A1_VECTOR
__interrupt void USCI_A0_ISR(void)
{
  switch(__even_in_range(UCA1IV,USCI_UART_UCTXCPTIFG))
  {
    case USCI_NONE: break;
    case USCI_UART_UCRXIFG:
      break;
    case USCI_UART_UCTXIFG:
      break;
    case USCI_UART_UCSTTIFG: break;
    case USCI_UART_UCTXCPTIFG:
      break;
    default: break;
  }
}

void configurare_timer(){
    TB0CCTL0 |= CCIE; // TBCCR0 interrupt enabled
    TB0CCR0 = 65535;
    TB0CTL = TBSSEL__ACLK | MC__UP | TBCLR; // TBC

    TB2CCR0 = 1000-1;                       // PWM Period/2
    TB2CCTL1 = OUTMOD_6;                    // TBCCR1 toggle/set
    TB2CCR1 = 0;                            // TBCCR1 PWM duty cycle
    TB2CTL = TBSSEL__SMCLK | MC_1 | TBCLR;  // SMCLK, up mode, clear TBR
}


#pragma vector = TIMER0_B0_VECTOR
__interrupt void Timer_B (void)
{
    ADCCTL0 |= ADCENC | ADCSC;
}

void configure_adc(){

    P5SEL1 |= BIT1;  // sensor
    P5SEL0 |= BIT1;

    // Configure ADC - Pulse sample mode; ADCSC trigger
    ADCCTL0 |= ADCSHT_15 | ADCON;                          // ADC ON,temperature sample period>30us
    ADCCTL1 |= ADCSHP;                                     // s/w trig, single ch/conv, MODOSC
    ADCCTL2 &= ~ADCRES;                                    // clear ADCRES in ADCCTL
    ADCCTL2 |= ADCRES_2;                                   // 12-bit conversion results
    ADCMCTL0 |= ADCSREF_0| ADCINCH_9;                      // ADC input ch A9
    ADCIE |= ADCIE0;
}

#pragma vector=ADC_VECTOR
__interrupt void ADC_ISR(void)
{
    switch(__even_in_range(ADCIV,ADCIV_ADCIFG))
    {
        case ADCIV_NONE:
            break;
        case ADCIV_ADCOVIFG:
            break;
        case ADCIV_ADCTOVIFG:
            break;
        case ADCIV_ADCHIIFG:
            break;
        case ADCIV_ADCLOIFG:
            break;
        case ADCIV_ADCINIFG:
            break;
        case ADCIV_ADCIFG:
            adcvalue = ADCMEM0;
            adcdone = true;
            break;
        default:
            break;
    }
}

void init_I2C(){

    P1SEL1 &= ~BIT3;            // P1.3 = SCL
    P1SEL0 |=  BIT3;

    P1SEL1 &= ~BIT2;            // P1.2 = SDA
    P1SEL0 |=  BIT2;

    UCB0CTLW0 |= UCSWRST;                    // Enable SW reset

    UCB0CTLW0 |= UCSSEL_3;
    UCB0BRW = 10;                            // fSCL = SMCLK/80 = ~100kHz

    UCB0CTLW0 |= UCMODE_3 | UCMST | UCTR;    // I2C master mode, SMCLK
    UCB0I2CSA = 0x27;

    UCB0CTLW1 |= UCASTP_2;      // Auto stop when UCB0TBCNT reached
    UCB0TBCNT =0x01;            // Send 1 byte of data

    UCB0CTLW0 &= ~UCSWRST;      // Clear SW reset, resume operation

    UCB0IE |= UCTXIE0;          // Enable TX and RX interrupt
}


#pragma vector = EUSCI_B0_VECTOR
__interrupt void EUSCI_B0_I2C_ISR(void){
    UCB0TXBUF = TXBUF;
}

void I2C_Send (int value){
    UCB0CTLW0 |= UCTXSTT;
    TXBUF = value;
}

void pulseEnable (int value){
    I2C_Send (value | En);      // En high
    __delay_cycles(150);        // enable pulse must be >450ns
    I2C_Send(value  & ~En);     // En low
    __delay_cycles(1500);       // commands need > 37us to settle
}
void write4bits (int value) {
    I2C_Send (value);
    __delay_cycles(50);
    pulseEnable (value);
}
void LCD_Send(int value, int mode) {
    int high_b = value & 0xF0;
    int low_b = (value << 4) & 0xF0;            // Shift the bits to the left and then set all bits except the ones in 0xF0 to 0.
    write4bits ( high_b | mode);                // write4bits is a function call with one arg and the arg is the result of a bitwise or | (one pipe symbol).
    write4bits ( low_b  | mode);                // The arg of write4bits uses 4 bits for the value and 4 bits for the mode.
                                                // It is being called first with high bits, then with th elow bits to write 8 bits.
}
void LCD_Write (char *text){
    unsigned int i;
    for (i=0; i < strlen(text); i++){
        LCD_Send((int)text[i], Rs | LCD_BACKLIGHT);
    }
}
void LCD_WriteNum(unsigned int num) {
    unsigned int reverseNum = 0;
    unsigned int digits = 0;                    // To use as a digit counter. For now, no digits are counted yet until we enter the first while loop.
    int i;                                      // This is for the for loop to run digits iterations.

    if (num == 0) {                             // If the user input 0 on the keypad...
        LCD_Send(0 | 0x30, Rs | LCD_BACKLIGHT); // ...then display 0 for 0% duty cycle.
    }
    else {

        while (num > 0) {
            reverseNum = reverseNum * 10 + (num % 10);
            num /= 10;
            digits++;                           // Increment digits; this means it is counting how many digits the user input from the keypad.
        }

        for(i = 0; i < digits; ++i) {           /* It will run digits iterations while it does the modulo and division operation. Now it knows how many digits
                                                it will print. This fixes the zeroes issue; now it can display #0 and 100 on the LCD successfully. */
            LCD_Send((reverseNum % 10) | 0x30, Rs | LCD_BACKLIGHT);
            reverseNum /= 10;
        }
    }
}
void LCD_SetCursor(int col, int row) {      // This function converts a column and row to a single number the LCD is expecting to set the cursor position.
    int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 }; // The LCD must have an interface where the starting position of each row is 0x00, 0x40, 0x14, and 0x54.
    LCD_Send(LCD_SETDDRAMADDR | (col + row_offsets[row]),  LCD_BACKLIGHT);
}
void LCD_ClearDisplay(void){
    LCD_Send(LCD_CLEARDISPLAY,  LCD_BACKLIGHT);
    __delay_cycles(50);
}
void LCD_leftToRight(void) {
    LCD_Send(LCD_ENTRYMODESET | LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT,  LCD_BACKLIGHT);
}
void LCD_rightToLeft(void) {
    LCD_Send(LCD_ENTRYMODESET | LCD_ENTRYRIGHT | LCD_ENTRYSHIFTDECREMENT,  LCD_BACKLIGHT);
}
void LCD_Setup(void){
    int _init[] = {LCD_init, LCD_init, LCD_init, LCD_4_BIT};
    int _setup[5];
    int mode = LCD_BACKLIGHT;
    _setup[0] = LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS;
    _setup[1] = LCD_CLEARDISPLAY;
    _setup[2] = LCD_RETURNHOME;
    _setup[3] = LCD_ENTRYMODESET | LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
    _setup[4] = LCD_DISPLAYCONTROL | LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;

    write4bits(_init[0]);   // Waiting for the enable function to be written.
    __delay_cycles(108000);//__delay_cycles(4500*us);  <--- equivalent to this.      It is the value we need to establish between the enable.
    write4bits(_init[1]);
    __delay_cycles(108000);
    write4bits(_init[2]);
    __delay_cycles(3600);
    write4bits(_init[3]);

    LCD_Send(_setup[0], mode);
    LCD_Send(_setup[1], mode);
    __delay_cycles(50);
    LCD_Send(_setup[2], mode);
    LCD_Send(_setup[3], mode);
    LCD_Send(_setup[4], mode);
}


