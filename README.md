# EsHard
Прошивка для плат СУЭ-П1 на базе ATmega128rfa1

## Компиляция
``` 
make all ST=ARW SA=3
```
`ST - тип устройства.`

Возможнные варианты:
  1. `ARW` - световой указатель направления (стрелка)
  2. `LHT` - световой сигнализатор разрешения использования проема (светофор)
  3. `BTN` - ручной пожарный извещатель (кнопка)

`SA - адрес устройства.`
 
Минимальное значение для адреса - 1, максимальное 127

## Прошивка
Программатор `usbasp`
```
make flush
```
