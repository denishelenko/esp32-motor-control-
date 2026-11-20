# Каретки 12GAN20-20: короткий посібник

## Що знаходиться в папках

| Папка або файл | Призначення |
| --- | --- |
| [Arduino IDE](<Arduino IDE>) | Скетчі: одна каретка з сайтом або дві ESP32-S3, де перша тримає сайт. |
| [Home dev internet](<Home dev internet/2_Carriages_WebSite>) | Дві ESP32-S3 підключаються до наявної мережі 2,4 ГГц; підтримуються сайт і OTA. Підпапка `NodeMCU` — старіший Home-скетч. |
| [PlatformIO](PlatformIO) | Проєкт для VS Code; використовує ті самі скетчі. Відкривайте саме цю папку. |
| [docs](docs) | Схема, детальні інструкції та калібрування; попередні матеріали — в `docs/archive`. |
| [Демо однієї каретки](web-interface-demo.html) | Візуальне демо в браузері. |
| [Демо двох кареток](web-interface-two-carriages-demo.html) | Візуальне демо в браузері. |

Демо не керують платами. Реальний сайт працює всередині прошитої ESP.

## Яку прошивку вибрати

| Варіант | Скетч Arduino IDE | Профіль PlatformIO |
| --- | --- | --- |
| Одна ESP32-S3 із сайтом | `Arduino IDE/1_Carriage_WebSite/ESP32S3/ESP32S3.ino` | `single_esp32s3` |
| Калібрування малого двигуна за часом | [Calibration_ESP32S3.ino](<Arduino IDE/Calibration_ESP32S3/Calibration_ESP32S3.ino>) | `calibration_esp32s3` |
| Одна NodeMCU | `Arduino IDE/1_Carriage_WebSite/NodeMCU/NodeMCU.ino` | `single_nodemcu` |
| Одна Wemos D1 mini Pro | `Arduino IDE/1_Carriage_WebSite/Wemos/Wemos.ino` | `single_d1_mini_pro` |
| Дві ESP32-S3: головна із сайтом і власним Wi‑Fi | `Arduino IDE/2_Carriages_WebSite/ESP32S3_Controller_Carriage_1/ESP32S3_Controller_Carriage_1.ino` | `dual_esp32s3_controller` |
| Дві ESP32-S3: друга каретка | `Arduino IDE/2_Carriages_WebSite/ESP32S3_Carriage_2/ESP32S3_Carriage_2.ino` | `dual_esp32s3_remote` |
| Дві ESP32-S3: Home, головна | `Home dev internet/2_Carriages_WebSite/ESP32S3_Controller_Carriage_1/ESP32S3_Controller_Carriage_1.ino` | `dual_home_main_serial` |
| Дві ESP32-S3: Home, друга | `Home dev internet/2_Carriages_WebSite/ESP32S3_Carriage_2/ESP32S3_Carriage_2.ino` | `dual_home_remote_serial` |

Для двох кареток прошийте кожну плату її відповідною версією. Для першого Home-прошивання використовуйте `serial`; подальше OTA — профілі `dual_home_main_ota` і `dual_home_remote_ota`.

## Як відкрити сайт

**Власний Wi‑Fi ESP:** підключіться до `Motor-Control` з паролем `motor8266` та відкрийте [http://motor-control.local/](http://motor-control.local/) або [http://192.168.4.1/](http://192.168.4.1/).

**Home:** впишіть назву й пароль мережі 2,4 ГГц у основний файл `.ino` кожної плати для парного режиму або у `PlatformIO/src/single_home_esp32s3.cpp` для однієї каретки. Підключіть телефон до тієї ж мережі та відкрийте [http://motor-control.local/](http://motor-control.local/). Якщо ім’я не знаходиться, використайте IP головної ESP із Serial Monitor. Пароль не публікуйте. [Детальніше про Home та OTA](<Home dev internet/2_Carriages_WebSite/README.md>).

## Підключення та калібрування

Для ESP32-S3 з драйвером TB6612FNG, сервоприводом і MOSFET підсвітки використовуйте [актуальні таблиці підключення](docs/WIRING_ESP32S3_TB6612FNG.md).

### Вимірювання відстані за часом

1. Відкрийте `Calibration_ESP32S3.ino` в Arduino IDE, виберіть плату `ESP32S3 Dev Module` і прошийте. У PlatformIO виберіть профіль `calibration_esp32s3`. Скетч використовує ESP32-S3 і TB6612FNG за [таблицею підключення](docs/WIRING_ESP32S3_TB6612FNG.md).
2. На телефоні підключіться до `Motor-Calibrate` з паролем `motor8266` та відкрийте [192.168.4.1](http://192.168.4.1/).
3. Почніть з короткого заїзду посередині троса. Задайте час від 0,2 до 10 с і ШІМ від 1 до 100%, натисніть «Вліво» або «Вправо». Заїзд зупиниться за таймером, при втраті зв’язку зі сторінкою або після натискання «СТОП».
4. Після зупинки виміряйте пройдену відстань рулеткою та введіть її в сантиметрах. Таблиця покаже час подачі ШІМ, відстань і середню швидкість. Заміряйте обидва напрямки за однакового ШІМ. Час включає розгін, а виміряна відстань включає вибіг.
5. Виставте каретку на фізичній середині троса й натисніть «Позначити центр (0 м)». Позначка діє до перезапуску калібрувальної прошивки. Таблиця вимірів зберігається в браузері.

Центр у цьому скетчі є міткою. Рух задається лише часом, тому перед кожним заїздом перевірте запас троса в обраному напрямку. Після калібрування використайте таблицю замірів, щоб налаштувати робочу прошивку.

Трос — 20 м; із запасом 2 м з кожного боку робоча відстань 16 м. Фактичний шлях оцінюється за часом: прошивка не отримує його від датчика. [Калібрування двох кареток](docs/CALIBRATION_TWO_ESP32.md); [інструкція для однієї каретки](docs/README_STANDALONE_LED.md); [деталі двох кареток](docs/README_TWO_CARRIAGES.md).

Показ швидкості задає PWM, а не виміряні RPM. Перед використанням механізм потрібно відкалібрувати. Серійний монітор — 115200 бод; швидкість USB-прошивання ESP32-S3 у PlatformIO — 921600 бод.
