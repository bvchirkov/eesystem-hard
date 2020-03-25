# EsHard
Прошивка для плат СУЭ-П1 на базе ATmega128rfa1.

На физическом уровне устройства соединены по интерфесу RS485. С помощью преобразователя RS485 to TTL осуществляется подключение к интерфесу UART. Скорость UART выставлена на уровне 9600.
<hr>

## Компиляция
``` 
make all ST=ARW SA=3
```
`ST - тип устройства`

Возможнные варианты:
  1. `ARW` - световой указатель направления (стрелка)
  2. `LHT` - световой сигнализатор разрешения использования проема (светофор)
  3. `BTN` - ручной пожарный извещатель (кнопка)

`SA - адрес устройства`
 
Минимальное значение для адреса - 1, максимальное 127.

## Прошивка
Программатор `usbasp`
```
make flush
```
<hr>

## Протокол обращения

Каждое устройство на линии ждет/отправляет 3 байта - пакет.

**Входящий пакет**

1-й байт - адрес устройства (`1 - 127` - индивидуальный, `128` - ширововещательный).

2-й байт - тип команды (`SET (0x01)` - отправка команды, `STATUS (0x00)` - запрос состояния устройства).

3-й байт - данные команды:
- `CMD_OFF (0x00)` - отключить устройство
- `CMD_LEFT (0x01)` - включить стрелку влево
- `CMD_RIGHT (0x02)` - включить стрелку вправо
- `CMD_STOP (0x01)` - включить светофор в режиме стоп (запрет движения через проем)
- `CMD_GO (0x02)` - включить светофор в режиме идти (разрешение движения через проем)

В качестве широковещательной команды может быть только `CMD_OFF (0x00)`

*Пример пакета для включения стрелки с адресом 1 вправо:*
```
0x01 0x01 0x02
```