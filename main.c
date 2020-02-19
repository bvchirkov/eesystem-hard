//SLAVE
#define F_CPU 16000000UL //частота процессора

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

#define ARW		( 0x01 ) // Стрелка
#define BTN		( 0x02 ) // Кнопка
#define LHT		( 0x03 ) // Светофор

#define BROADCAST_ADDR	( 0x80 ) // Общий для всех адрес
//#define SLAVE_ADDR	( 0x01 ) // Индивидуальный адрес (Max 0x7F-127)
/*----------------------------------------------------------------------
 Активный тип устройства
----------------------------------------------------------------------*/
//#define SLAVE_TYPE	ARW

/*----------------------------------------------------------------------
 Интервал в тиках МК между миганиями стрелки и светофора
----------------------------------------------------------------------*/
#define BLINK_INTERVAL	( 100000 )	

/*----------------------------------------------------------------------
 Настройки скорости передачи данных по UART
----------------------------------------------------------------------*/
#define USART_BAUDRATE 9600
#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

/*----------------------------------------------------------------------
 Концигурирование UART
 TX на порту PE1, RX на порту PE0
 Включено прерывание по RX.
 Длина пакета 8 бит + 1 стоп бит.
----------------------------------------------------------------------*/
void init_usart()
{
    DDRE &= ~(1 << PE0);        // RX
    DDRE |=  (1 << PE1);        // TX
    
    UBRR0H = (uint8_t)(BAUD_PRESCALE >> 8);
    UBRR0L = (uint8_t) BAUD_PRESCALE;

    // Enable receiver and transmitter. Enable iterrupt by RX
    UCSR0B =  (1 << TXEN0)
            | (1 << RXEN0)
            | (1 << RXCIE0);

    // Set frame format: 1 stop bit(USBS0), 8 data(UCSZ00)
    UCSR0C =   (0 << UMSEL01) | (0 << UMSEL00) //Async UART
             | (0 << UPM01)   | (0 << UPM00)   //
             | (0 << USBS0)   //0 - one stop bit, 1 - two stop bits
             | (0 << UCSZ02)  | (1 << UCSZ01) | (1 << UCSZ00);  //8 bit
}

/*----------------------------------------------------------------------
 Запись в регистр UDR0 -> передача сообщения
----------------------------------------------------------------------*/
uint8_t USART0_TX (uint8_t aByte)
{
    //UART not ready to TX
    if (!(UCSR0A & (1 << UDRE0)))
    {
        return -1;
    }

    UDR0 = aByte;
    return 0;
}

/*----------------------------------------------------------------------
 Чтение регистра UDR0 -> получение сообщения
----------------------------------------------------------------------*/
int USART0_RX (void)
{
    //UART not ready to RX
    if (!(UCSR0A & (1 << RXC0)))
    {
	return -1;
    }

    return UDR0;
}

/*----------------------------------------------------------------------
 Счетчик байтов пакета
 Максимальное количество байтов - 3
----------------------------------------------------------------------*/
static volatile uint8_t index = 0;

/*----------------------------------------------------------------------
 Возможные значения второго байта данных 
----------------------------------------------------------------------*/
#define STATUS		( 0x00 )
#define SET		( 0x01 )

/*----------------------------------------------------------------------
 Возможные значения третьего байта данных
 Данные этого байта учитываются, только если второй байт SET
----------------------------------------------------------------------*/
// Сброс регистра состояния для любого устройства
#define CMD_OFF		( 0x00 )
// Для стрелки
#define CMD_LEFT	( 0x01 )	
#define CMD_RIGHT	( 0x02 )	
// Для светофора
#define CMD_STOP	( 0x01 )
#define CMD_GO		( 0x02 )

/*----------------------------------------------------------------------
 76543210
 7 бит - выставляется как признак того, что команда получена верно
 6 бит - для стрелки и светофора бит блокировки. Блокировка используется
	 чтобы избавиться от выбора режима на каждом тике.
 5 бит - для стрелки и светофора бит состояния - вкл/выкл.
 4 бит - состояние кнопки (1 - была нажата, 0 - не нажата)
 3 бит - 
 2 бит - 
 1 и 0 бит - команда для стрелки или светофора
----------------------------------------------------------------------*/
typedef union
{
    struct
    {
	uint8_t cmd   : 2;
	uint8_t b2    : 1;
	uint8_t b3    : 1;
	uint8_t btn0  : 1;
	uint8_t state : 1;
	uint8_t lock  : 1;
	uint8_t b7    : 1;
    } bits;
    uint8_t rdata;
} Reg;

/*----------------------------------------------------------------------
 Регистр состояний устройства
----------------------------------------------------------------------*/
static Reg reg_ss;

/*----------------------------------------------------------------------
 Структура приходящего пакета
----------------------------------------------------------------------*/
typedef struct Pkg_t
{
    uint8_t addr;	// Адрес устройства
    uint8_t mode;	// Команда
    Reg	    data;	// Данные
} PKG;

/*----------------------------------------------------------------------
 Входящий пакет
----------------------------------------------------------------------*/
static volatile PKG pkg_in;

/*----------------------------------------------------------------------
 Отправка пакета
 Паузы нужны, чтобы не байты успевали отправляться
 
 TODO: Отправлять с помощью прерываний UART
----------------------------------------------------------------------*/
void send_pkg(volatile PKG * aPkg)
{
    _delay_ms(2);
    USART0_TX(aPkg->addr);
    _delay_ms(2);
    USART0_TX(aPkg->mode);
    _delay_ms(2);
    USART0_TX(aPkg->data.rdata);
}

/*----------------------------------------------------------------------
 Обработка входящего пакета, как индивидуального, так и 
 широковещательного.
 В случае широковещательного пакета, принимаем только команду CMD_OFF.
----------------------------------------------------------------------*/
void pkg_handler(volatile PKG * aPkg, Reg * aRegSS)
{
    if (aPkg->addr == SLAVE_ADDR)
    {
	if (aPkg->mode == STATUS)
	{ 
	    aPkg->data.rdata = aRegSS->rdata;
	}
	aPkg->data.bits.b7 = 1;
	send_pkg(aPkg);
    } else if (aPkg->addr == BROADCAST_ADDR)
    {
	if (aPkg->mode != SET && 
	    aPkg->data.bits.cmd != CMD_OFF)
	{
	    return;
	}
    } else return;
    
    aRegSS->rdata = aPkg->data.rdata;
}

/*----------------------------------------------------------------------
 Настройки стрелки
----------------------------------------------------------------------*/
#define ARW_DDR			DDRE
#define ARW_PORT		PORTE
#define ARW_SIDE_LEFT		PE6
#define ARW_CENTER		PE5
#define ARW_SIDE_RIGHT		PE4

#if (SLAVE_TYPE == ARW)
void arw_init()
{
    ARW_DDR  |= (1 << ARW_SIDE_LEFT)|(1 << ARW_CENTER)|(1 << ARW_SIDE_RIGHT);
    ARW_PORT |= (1 << ARW_SIDE_LEFT)|(1 << ARW_CENTER)|(1 << ARW_SIDE_RIGHT);
    _delay_ms(1000);
    ARW_PORT &= ~((1 << ARW_SIDE_LEFT)|(1 << ARW_CENTER)|(1 << ARW_SIDE_RIGHT));
}

/*----------------------------------------------------------------------
 Выбор стороны, которая должна мигать
----------------------------------------------------------------------*/
void arw_choose_side(Reg * aRegSS, uint8_t * aArwSide)
{
    if (aRegSS->bits.lock == 0)
    {
	aRegSS->bits.lock = 1;
	if (*aArwSide != 0) ARW_PORT &= ~(1 << *aArwSide);
	if (aRegSS->bits.cmd == CMD_LEFT)
	{
	    *aArwSide = ARW_SIDE_LEFT;
	} else if (aRegSS->bits.cmd == CMD_RIGHT)
	{
	    *aArwSide = ARW_SIDE_RIGHT;
	}
    }
}

/*----------------------------------------------------------------------
 Мигание и отключение стрелки
----------------------------------------------------------------------*/
void arw_handler(Reg * aRegSS)
{
    static uint8_t arw_side = 0;
    if (aRegSS->bits.cmd == CMD_OFF)
    {
	ARW_PORT &= ~((1 << ARW_SIDE_LEFT)|(1 << ARW_SIDE_RIGHT)|(1 << ARW_CENTER));
    } else
    {
	arw_choose_side(aRegSS, &arw_side);
	
	if (aRegSS->bits.state == 0)
	{ // OFF
	    ARW_PORT &= ~((1 << arw_side)|(1 << ARW_CENTER));
	} else
	{ // ON
	    ARW_PORT |=  (1 << arw_side)|(1 << ARW_CENTER);
	}
    }
}
#endif

/*----------------------------------------------------------------------
 Настройки кнопки
----------------------------------------------------------------------*/
#define BTN_DDR		DDRE
#define BTN_PORT	PORTE
#define BTN_PIN		PINE
#define BTN_CHANEL_0	PE2   // Порт
#define BTN_EXPECT	65000 // Количество тиков ожидания подтверждения

#if (SLAVE_TYPE == BTN)
void btn_init()
{
    BTN_DDR  &= ~(1 << BTN_CHANEL_0);
    BTN_PORT |=  (1 << BTN_CHANEL_0);
}

void btn_handler(Reg * aRegSS)
{
    static uint16_t btn_ticks_counter = 0;
    if ((BTN_PIN & (1 << BTN_CHANEL_0)) == 0)
    {
	btn_ticks_counter++;
    } else
    {
	btn_ticks_counter = 0;
    }
    
    if (btn_ticks_counter == BTN_EXPECT)
    {
	aRegSS->bits.btn0 = 1;
    }
}
#endif

/*----------------------------------------------------------------------
 Настройки светофора
----------------------------------------------------------------------*/
#define LHT_PORT	PORTE
#define LHT_DDR		DDRE
#define LHT_MODE_GO	PE5
#define LHT_MODE_STOP	PE6

#if (SLAVE_TYPE == LHT)
void lht_init()
{
    LHT_DDR  |= (1 << LHT_MODE_GO) | (1 << LHT_MODE_STOP);
    LHT_PORT |= (1 << LHT_MODE_GO) | (1 << LHT_MODE_STOP);
    _delay_ms(1000);
    LHT_PORT &= ~((1 << LHT_MODE_GO) | (1 << LHT_MODE_STOP));
}

/*----------------------------------------------------------------------
 Выбор режима работы светофора
----------------------------------------------------------------------*/
void lht_choose_mode(Reg * aRegSS, uint8_t * aLhtMode)
{
    if (aRegSS->bits.lock == 0)
    {
	aRegSS->bits.lock = 1;
	if (*aLhtMode != 0) LHT_PORT &= ~(1 << *aLhtMode);
	if (aRegSS->bits.cmd == CMD_GO)
	{
	    *aLhtMode = LHT_MODE_GO;
	} else if (aRegSS->bits.cmd == CMD_STOP)
	{
	    *aLhtMode = LHT_MODE_STOP;
	}
    }
}

/*----------------------------------------------------------------------
 Мигание и отключение светофора
----------------------------------------------------------------------*/
void lht_handler(Reg * aRegSS)
{
    static uint8_t lht_mode = 0;
    if (aRegSS->bits.cmd == CMD_OFF)
    {
	LHT_PORT &= ~((1 << LHT_MODE_GO)|(1 << LHT_MODE_STOP));
    } else
    {
	lht_choose_mode(aRegSS, &lht_mode);
	
	if (aRegSS->bits.state == 0)
	{ // OFF
	    LHT_PORT &= ~(1 << lht_mode);
	} else
	{ // ON
	    LHT_PORT |=  (1 << lht_mode);
	}
    }
}
#endif

/*----------------------------------------------------------------------
 Обертка для используемой в текущий момент конфигурации устройства
----------------------------------------------------------------------*/
void reg_ss_handler(Reg * aRegSS)
{
#if (SLAVE_TYPE == ARW)
    arw_handler(aRegSS);
#elif (SLAVE_TYPE == BTN)
    btn_handler(aRegSS);
#elif (SLAVE_TYPE == LHT)
    lht_handler(aRegSS);
#endif
}

int main ()
{
    SREG &= ~(1 << 7);    //The global interrupt disable
    init_usart();
    
#if (SLAVE_TYPE == ARW)
    arw_init();
#elif (SLAVE_TYPE == BTN)
    btn_init();
#elif (SLAVE_TYPE == LHT)
    lht_init();
#endif
    
    uint32_t counter_ticks = 0; // max 65530
    SREG |=  (1 << 7);    //The global interrupt enable

    while (1)
    {
	if (index == 3)
	{
	    pkg_handler(&pkg_in, &reg_ss);
	    index = 0;
	}
	reg_ss_handler(&reg_ss);
	
	if (counter_ticks == BLINK_INTERVAL)
	{
	    reg_ss.bits.state = reg_ss.bits.state == 0 ? 1 : 0;
	    counter_ticks = 0;
	}
// Для кнопки отключил, чтоб не засорял регистр
// Можно красивее, но пока видимо так =)	
#if (SLAVE_TYPE != BTN)
	counter_ticks++;
#endif
    }
}

//USART0_RX             USART0 Rx Complete
ISR(USART0_RX_vect)
{
    if (index == 0)
    {
	pkg_in.addr = UDR0;
    } else if (index == 1)
    {
	pkg_in.mode = UDR0;
    } else if (index == 2)
    {
	pkg_in.data.rdata = UDR0;
    }
    
    index++;
}