GDP075FW1: проверка режимов обновления GDEM075F52
================================================

Плата: ESP32-M075, ESP32-WROOM-32D.
Панель: 800 x 480, четыре цвета, GDEM075F52 / JD79665AA.
Версия приложенного приложения: 0.1.0-refresh-tests.2815758acf4e.

Это дополнение к существующему GDP075FW1 для испытаний панели. Будущий
демосценовый проект остаётся отдельным проектом. Промпт Codex не изменён.

СОДЕРЖИМОЕ

firmware-refresh-tests.bin     Собранное приложение ESP32, 865168 байт.
gdp075fw1-refresh-modes.patch  Все изменения исходников, документации и тестов.
patterns/baseline.native      Контрольная картинка A, 96000 байт.
patterns/candidate.native     Контрольная картинка B, 96000 байт.
patterns/baseline.ppm         Предпросмотр A.
patterns/candidate.ppm        Предпросмотр B.
patterns/partial-expected.ppm Ожидаемое A снаружи / B внутри тестового окна.
VALIDATION.json               Версии зависимостей и результаты проверок.
SHA256SUMS                    Контрольные суммы файлов этого комплекта.

ПРОВЕРКИ И ГРАНИЦЫ РЕЗУЛЬТАТА

Пройдены 9 C++ suites с AddressSanitizer/UBSan и 14 Python-тестов.
Собраны настоящие обработчики main.cpp и настоящий драйвер с имитацией
GPIO/SPI/BUSY, файловой системы и событий Arduino WebServer. Сам байтовый
HTTP/multipart parser не эмулируется. Патч проверен на чистом исходном дереве:
все 22 изменённых файла воспроизводятся побайтно.

Реальная сборка ESP32 успешно выполнена с PlatformIO Core 6.2.0,
espressif32@7.1.3, GxEPD2@1.6.9, Adafruit GFX@1.12.6 и BusIO@1.17.4.
Проверены формат ESP32 image, размеры сегментов, checksum и встроенный SHA-256.
Новые режимы на физической панели ещё не проверены. Успех HTTP означает
завершение команд/BUSY без обнаруженной ошибки, а не оптическую проверку.

Публикация изменений в GitHub не выполнена: подключение вернуло
403 Resource not accessible by integration при создании Git tree.
Ветка и PR не созданы. Все изменения доступны в приложенном патче.

УСТАНОВКА ГОТОВОГО ПРИЛОЖЕНИЯ

Из каталога распакованного комплекта:

  sha256sum -c SHA256SUMS

firmware-refresh-tests.bin предназначен для записи по адресу 0x10000
при существующей разметке проекта GDP075FW1: app0 размером 0x140000,
app1 по 0x150000, SPIFFS по 0x290000. Это application image.
Пример с уже установленным esptool; замените путь к Python и USB-порт:

  ~/esptool-env/bin/python -m esptool --chip esp32 --port /dev/ttyUSB0 write_flash 0x10000 firmware-refresh-tests.bin

Такая запись обновляет область приложения при указанной разметке.
Сохранение существующей резервной копии и настройки подключения описаны
в README исходного GDP075FW1. После запуска GET /status должен показать
версию 0.1.0-refresh-tests.2815758acf4e.

ПРИМЕНЕНИЕ ИСХОДНИКОВ К СВОЕМУ GDP075FW1

База патча:
  https://github.com/iaon/GDP075FW1
  12cf0fce2637b827b3581e5925cd7de132a7f475

В своём checkout GDP075FW1 создайте рабочую ветку, затем проверьте и примените
патч. В командах ниже замените /path/to/bundle на каталог распакованного архива:

  git switch -c panel-refresh-tests
  git apply --check /path/to/bundle/gdp075fw1-refresh-modes.patch
  git apply /path/to/bundle/gdp075fw1-refresh-modes.patch
  python3 scripts/run_tests.py --sanitize

Если локальные изменения затрагивают те же строки, git apply --check покажет
конфликт до применения. Подробный протокол появится в docs/REFRESH-MODES.md.
Для самостоятельной сборки используйте обычный scripts/pio.sh проекта.

ENDPOINTS

POST /frame/native
  Обычный полный refresh, прежняя последовательность команд.

POST /frame/native/fast-full
  Экспериментальный быстрый полный refresh.
  mode=fast_full.

POST /frame/native/partial?x=200&y=120&w=400&h=240
  Экспериментальное обновление окна с обычной waveform.
  mode=partial.

POST /frame/native/fast-window?x=200&y=120&w=400&h=240
  Эксперимент: fast-full waveform совместно с оконной адресацией.
  mode=fast_window_experimental. Это не подтверждённый fast partial.

GET /display/modes
  Флаги базовой библиотеки, флаг vendor API, маршруты и правила тестирования.

Старые POST /frame (две битовые плоскости), TCP :8000 и статусный экран
сохраняют обычный full refresh. Выбранный экспериментальный режим действует
только для текущего запроса; следующий обычный запрос вновь использует full.

В GxEPD2 1.6.9:
  hasPartialUpdate=true
  hasFastPartialUpdate=false
  useFastFullUpdate=false

Это свойства/настройка библиотеки, а не автоматическое обнаружение панели.
Vendor subclass сохраняет hasPartialUpdate=false для существующего paged API;
новые endpoints используют отдельный явно выбранный диагностический путь.
hasFastPartialUpdate не подменяется на true.

ФОРМАТ И ОКНО

Все /frame/native... принимают ровно один multipart file с именем frame.
Размер всегда 96000 байт: полная картинка 800 x 480, 2 bpp, 200 байт/строку.
Четыре пикселя на байт, старшие пары бит первыми:
  00 чёрный; 01 белый; 10 жёлтый; 11 красный.

В оконных режимах тоже передаётся полный файл. Все его байты загружаются в RAM
контроллера панели; затем запрашивается физический refresh только окна.
Предыдущий framebuffer в ESP32 не хранится, сохранённая RAM после sleep
не требуется. Объём HTTP/SPI передачи в этих опытах не сокращается.

Все четыре параметра x,y,w,h обязательны в query string ровно один раз.
x и w кратны 4; w >= 4; h >= 2; окно целиком внутри 800 x 480.
Округления, обрезания и автоматического перехода в full нет.
Окно 0,0,800,480 разрешено и всё равно проверяет PMODE=1.
Окно 200,120,400,240 означает x=200..599, y=120..359 включительно.

БЫСТРЫЙ ПРОТОКОЛ A/B

Запускайте команды из каталога распакованного комплекта и замените адрес:

  EPD_URL='http://192.168.1.208'
  curl --fail-with-body --max-time 180 "$EPD_URL/display/modes"

Для каждого режима сначала восстановите A обычным полным refresh:

  curl --fail-with-body --max-time 180 -F frame=@patterns/baseline.native "$EPD_URL/frame/native"

Затем отправьте B в один из четырёх endpoints:

  curl --fail-with-body --max-time 180 -F frame=@patterns/candidate.native "$EPD_URL/frame/native"

  curl --fail-with-body --max-time 180 -F frame=@patterns/candidate.native "$EPD_URL/frame/native/fast-full"

  curl --fail-with-body --max-time 180 -F frame=@patterns/candidate.native "$EPD_URL/frame/native/partial?x=200&y=120&w=400&h=240"

  curl --fail-with-body --max-time 180 -F frame=@patterns/candidate.native "$EPD_URL/frame/native/fast-window?x=200&y=120&w=400&h=240"

Перед каждым следующим опытом повторяйте восстановление baseline.native.
Дождитесь ответа на предыдущий запрос. При клиентском timeout дождитесь
окончания операции на панели/проверьте UART перед следующим запросом.

Полные режимы должны показать candidate.ppm. Оконные — результат,
похожий на partial-expected.ppm: B внутри, A снаружи прямоугольника.
Контрольные изображения различаются снаружи окна специально: иначе случайный
полный refresh трудно отличить от правильного partial. Пара покрывает все
16 переходов между четырьмя цветами. PPM-файлы предназначены для просмотра,
а не для загрузки в устройство.

Для собственных PNG/JPEG используйте существующий конвертер из раздела
Uploading a picture from Linux в README проекта, затем отправляйте полученный
полный .native выбранному endpoint. scripts/device.py отправляет через TCP
и не переключает экспериментальные режимы.

КАК СРАВНИВАТЬ

Ответ содержит result, mode, experimental, window_x/y/w/h, frame_bytes,
received_bytes, refresh_started, upload_ms, init_ms, write_ms, refresh_ms,
sleep_ms, total_ms и error. Успешный ответ начинается с display refreshed.

refresh_ms: отправка команды refresh и ожидание BUSY.
write_ms: чтение staged-файла и передача всех 96000 байт в контроллер.
init_ms: reset и настройка, включая fast setup, если он выбран.
sleep_ms: power-off/hibernate после операции.
upload_ms: от первого multipart FILE_START до завершения staging.
total_ms: от FILE_START до завершения display cleanup и подготовки отчёта.
Это не включает установление HTTP-соединения до первого FILE_START.

Для каждого A->B опыта запишите mode, размер окна, температуру, времена,
остатки предыдущего изображения, цвета и изменения/мигание снаружи окна.
Сравнивайте refresh_ms отдельно от upload_ms. Скорость и отсутствие ghosting
не гарантируются программным endpoint; измерьте их на конкретной панели.

ПЕРВИЧНЫЕ ИСТОЧНИКИ

https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/epd4c/GxEPD2_750c_GDEM075F52.h
https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/epd4c/GxEPD2_750c_GDEM075F52.cpp
https://www.laskakit.cz/user/related_files/jd79665aa_datasheet_v1.pdf

Fast setup использует E0/02, E6/5A, A5/00 из GxEPD2 1.6.9; окно — команду
0x83 и PMODE=1, описанные в JD79665AA §8.2.24. Произвольные LUT не добавлены.
Производительность PMODE и сочетания fast+window должна быть проверена
описанными выше физическими тестами.
