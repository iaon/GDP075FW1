# GDEM075F52 / JD79665AA: одиночные PLL-опыты и граница LUT

Локальная реализация от 2026-10-22. Существующие HTTP/TCP/status пути,
пины, 10 MHz SPI, побайтовый CS/DC, файловый staging и partitions сохранены.
Демо, MTP/OTP/eFuse programming, произвольные регистры и изменение напряжений
не реализуются. Новый исполняемый путь — временный PLL; register LUT заблокирован.

## Что известно и откуда

| Статус | Основание |
|---|---|
| Документировано для JD79665AA | S1: PLL, LUT_EN, DDX и ограничения BUSY |
| Взято из драйвера этой панели | S3: исходный `0x30=0x08`, существующий fast setup `E0/02 E6/5A A5/00` |
| Проверяется host-тестами | Разбор API, SPI trace, отсутствие register LUT и programming, восстановление, timings и ошибки; имитация WebServer callbacks, не его wire parser |
| Гипотеза для новой проверки | Последняя отправленная запись PLL после PON влияет на последующий refresh; физическая частота и внутренние перезагрузки не прочитаны |
| Проверено пользователем ранее | Измерения в таблице ниже; новая PLL-ветка пользователем ещё не проверена |

Источники, проверенные 2026-10-22:

- **S1:** [JD79665AA v1.0.0, 2023-08-25](https://www.laskakit.cz/user/related_files/jd79665aa_datasheet_v1.pdf).
  §8.2.11, печатная p36: `Dyna=bit3`, `FR=bits2..0`. Формула `(Dyna<<3)|FR`.
  §8.2.1 pp20–21: LUT_EN — bit7 **второго** байта PSR; 0=MTP, 1=register.
  §8.2.7 p32: DDX=1 выбирает LUT0..3 напрямую, DDX=0 инвертирует индексы.
  Текущие CDI 0x37/0x97 сохраняют DDX=1.
  §9.2 p69 описывает **сжатые** MTP-данные, не register payload;
  §9.4 pp71–72 показывает 0x20 в диаграмме без структуры таблицы.
  §8.2.9 p34 и таблица ограничений p62 запрещают обычные записи при BUSY.
  Таблицы pp20–21,32,36 проверены по растровым изображениям PDF.
- **S2:** [официальная панель](https://www.good-display.com/product/1136.html),
  [спецификация](https://www.good-display.com/companyfile/1520.html),
  [ESP32 sample](https://www.good-display.com/companyfile/1519.html).
  Страницы двух загрузок доступны через просмотр web, прямой HTTP GET sample
  вернул 403; архив не получен и его содержимое не считается проверенным.
  Нового подтверждённого register-LUT примера из этих загрузок нет.
- **S3:** GxEPD2 **tag 1.6.9**, [header](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/epd4c/GxEPD2_750c_GDEM075F52.h)
  и [implementation, `_InitDisplay()`](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/epd4c/GxEPD2_750c_GDEM075F52.cpp).
  Сверены с установленной закреплённой библиотекой `.pio/libdeps/gdp075fw1/GxEPD2`.
  Это источник штатной fast-последовательности, не register-LUT загрузчика.
- Дополнительно просмотрены [официальный аккаунт Good Display](https://github.com/gooddisplayepaper)
  и [DKE DEPG0583RYF665F10, rev2.4, 2025-01-25](https://www.dke.com.cn/uploads/file/20250306/6caffcb4e28bd4fa4efc86489ea8eec6.pdf),
  оглавление p3 указывает command table pp14–54 и OTP flow pp57–58. Это другая панель
  с JD79665AA; её waveform и напряжения в GDEM075F52 не переносились.
  Поиск `JD79665AA register LUT`, `JD79665AA 0x20`, application notes и
  официальных исходников GDEM075F52 не дал подтверждённой спецификации загрузки.
  LUT другого JD796xx, UC81xx, SSD16xx не считается совместимой.

Для снятия блокировки нужен документ Jadard **Register LUT programming/format
для JD79665AA**, включая точные команды, длины, порядок, уровни относительно
VCOM, длительности, повторы, конец, border и правила reload/temperature/Dyna;
затем — полный известный register-LUT профиль именно GDEM075F52 с примером
загрузки. Наличие LUT_EN/0x20 и дампа сжатой MTP этого не заменяет.

`patterns/lut-profiles.json` содержит пустой список профилей и требования к
доказательствам. Host validator проверяет обязательные metadata и SHA-256,
но отклоняет даже целый payload с неизвестным форматом. Ни одной подходящей
заводской таблицы, register-LUT загрузчика или B/W аппаратной waveform нет.
LUT_EN никогда не устанавливается, фиктивного fallback на fast-full нет.

## PLL и API

| FR | Номинал из S1, Hz | Dyna=0 | Dyna=1 | Начальный allowlist |
|---|---:|---|---|---|
| 0 | 12.5 | 0x00 | 0x08 | только 0x08 |
| 1 | 25 | 0x01 | 0x09 | только 0x09 |
| 2 | 50 | 0x02 | 0x0a | оба |
| 3 | 65 | 0x03 | 0x0b | только 0x03 |
| 4 | 75 | 0x04 | 0x0c | только 0x04 |
| 5 | 85 | 0x05 | 0x0d | только с extended=1 |
| 6 | 100 | 0x06 | 0x0e | только с extended=1 |
| 7 | 120 | 0x07 | 0x0f | только с extended=1 |

Остальные значения в пределах 0x00..0x0f тоже требуют `extended=1`.
Старшие биты запрещены всегда. Это номиналы контроллера, а не гарантированная
поддержка панелью. **Исходный 0x08 включает Dyna и не означает измеренные
постоянные 12.5 Hz.** Фактическую частоту не измеряет ни прошивка, ни инструмент.

- `GET /display/experiments`: capability report, версия/git, allowlist, источники,
  ограничения и `blocked_reason`. Без staging, SPI и аппаратных проб.
- `POST /frame/native/pll?base=fast-full&pll=0x09`: полный native frame,
  ровно 96000 байт, единственный multipart file `frame`.
  Требуется **`X-EPD-Experimental: 1`**.
- `base=full|fast-full|partial|fast-window`. Только для двух оконных обязательны
  ровно по одному `x,y,w,h`: x/w кратны 4, w>=4, h>=2, окно внутри 800×480.
  Порядок параметров произволен. Лишние/повторные/пустые параметры отклоняются.
  `pll` имеет строгий формат `0xHH`/`0XHH`, включая два hex-символа.
  Расширенный opt-in — единственное дополнительное поле `extended=1`.
- `POST /frame/native/lut?profile=ID`: с заголовком и одним `profile` возвращает
  **501** и причину **до staging и любых SPI-команд**, для любого ID. При
  malformed API — 400. Загрузить профиль по HTTP или выбрать существующий нельзя.
- `POST /display/recover`: тот же обязательный заголовок, никаких параметров,
  text fields или файлов. Reset, прежняя инициализация и **один обычный full
  белый кадр** 0x55. RAM — одна строка 200 байт; файлового staging нет.
  Затем можно загрузить исходную картинку обычным `/frame/native`.

Вся проверка параметров и завершённости upload предшествует воздействию.
Повторная проверка после multipart отвергает поздние поля/изменения параметров.
Повторные файлы запрещены. Header собирается штатным Arduino WebServer;
его parser сохраняет последнее значение повторного HTTP header, поэтому
повторные wire headers не считаются проверяемыми этим API. Не отправляйте их.
Это opt-in для лабораторного опыта, не механизм авторизации сети.

Порядок PLL: reset → рабочая инициализация с `0x30=0x08` → прежний fast setup,
если выбран → PON и ожидание idle → **выбранный 0x30** → весь кадр → DRF.
После выбранного значения нет более поздних программных записей PLL.
Этот порядок исключает его перезапись нашими setup-командами. Внутренний
readback/перезагрузка контроллера не проверены. После опыта — power-off/sleep;
следующий обычный запрос делает reset и снова использует штатный 0x08.
Настройки и request counter не сохраняются в NVS. Нет опытов при boot/reconnect.

`pll_written` — последний отправленный байт, `pll_readback=false`.
На этой ESP32-M075 сейчас используется write-only SPI без настроенного MISO.
Теоретический bidirectional SDA требует проверки схемы и read-протокола;
его здесь не предполагаем, MOSI в input не переключаем. MTP readback не реализован.

## BUSY, отчёт и восстановление

`refresh_ms` охватывает отправку DRF и ожидание BUSY до idle. Для новых PLL
и recovery требуется увидеть LOW не позднее 100 ms; иначе HTTP 500 с
`refresh BUSY did not assert; outcome unknown`, даже если пин был HIGH всё время.
Один нормальный BUSY подтверждает только последовательность, не правильную картинку.
Порог 100 ms — диагностический запас, не измеренная задержка этой платы.

Сохраняются старые `upload_ms,init_ms,write_ms,refresh_ms,sleep_ms,total_ms`.
Добавлены request_id (счётчик только текущей загрузки ESP32), firmware_version/git,
base_mode, window, pll_requested/written, Dyna, FR, nominal_hz, lut_source,
profile_id/hash, pixel_encoding, lut_load_ms, cleanup_ms, refresh_count,
refresh_started, busy_assert_seen, cleanup_state/error, blocked_reason.
Формат `key=value`, по одной строке; необязательная первая строка `display refreshed`.
LUT сейчас factory_MTP, profile_id/hash=none, lut_load_ms=0.
На отклонённом запросе pll_written=unknown, не значение от предыдущей операции.

- upload: первый FILE_START до завершения/валидации staging.
- init: reset/setup/PON/выбранная PLL; LUT-загрузка сюда не включается.
- write: чтение строк и передача полного кадра в RAM контроллера.
- refresh: DRF и ожидание физической последовательности.
- cleanup: power-off/sleep; `sleep_ms` — совместимый alias этого интервала.
- total: от FILE_START до подготовки ответа; для recovery — от начала handler.
  Он может содержать накладные расходы помимо суммы независимых стадий.

Для PLL refresh допускается до 180 s, все ожидания имеют общий бюджет 240 s
от начала драйвера; остальные отдельные ожидания сохраняют штатные 50 s.
Это запас для исследования более медленных частот: грубое масштабирование
22 s при 50 Hz к 12.5 Hz даёт ~88 s, но Dyna делает такую оценку гипотезой.
180 s — конечный диагностический предел, не длительность требуемой waveform.
Клиентский timeout 300 s оставляет запас на staging/передачу и ответ.
Watchdog обслуживается `delay(1)`. Первая ошибка сохраняется.

После timeout при BUSY=LOW **не отправляются** power-off или sleep; это также
исправлено для старых путей. Отчёт показывает skipped_busy/unknown power state.
При timeout POF sleep не отправляется, cleanup_error=power_off_timeout.
При отсутствии ожидаемого BUSY cleanup также пропускается: исход неизвестен,
и автоматический power-off мог бы прервать не замеченное начало обновления.
Новый запрос/recovery — явная точка восстановления через reset. Синхронный
WebServer не обслужит recovery немедленно, пока текущий handler ждёт BUSY.
При зависании дождитесь предела, прочитайте UART; запасной путь — ручное
отключение/включение питания, затем штатный baseline. После перезапуска
эксперимент не повторяется. Таймаут клиента не означает остановку платы.

UART `EPD cfg cmd=0x.. n=.. ...` выводит только конфигурационные команды новых
опытов, не 96 KB пикселей; итоговый отчёт `DISPLAY HTTP` содержит timings.

Остаточный риск: изменённая частота/waveform может вызвать ghosting, потерю
цвета или долговременное изменение панели. Конечные ожидания и штатный reset
не гарантируют безопасность или устранение последствий. HOLD не равен
автоматически нулевому source: важны общий VCOM и остаточное напряжение TFT.
Чёрно-белые logical transition IDs не являются электрическими фазами.
0.5–2 s остаётся исследовательской гипотезой, результат не обещается.

## Инструмент и картинки

`scripts/epd_experiments.py` использует только Python stdlib. Нет новых pip
зависимостей. `generate` не открывает Serial/сеть/SPI; создаёт `.native` и
`.ppm` из **тех же упакованных байтов**, плюс manifest с SHA-256.

| Файлы .native и соответствующие .ppm | Назначение |
|---|---|
| baseline, candidate | Четыре цвета, все 16 переходов, тонкие линии; различия внутри и вне 200/120/400/240 |
| partial-expected | Baseline вне большого окна, candidate внутри |
| bw-before, bw-after, bw-expected | Все WW/WB/BW/BB в маленьком окне 200/120/80/40 |
| square-before, square-after, square-expected | Сдвиг квадрата: старый участок белеет, новый чернеет, перекрытие остаётся |
| unchanged | Совпадает с bw-before, тест неизменного изображения |
| white | Штатный белый кадр |

В B/W парах жёлтый и красный контрольные участки находятся только снаружи
окна. `bw-transitions.host-only` содержит logical IDs WW=0, WB=1, BW=2, BB=3
(MSB-first), **это не native-colour payload и не аппаратная LUT**. Не загружайте
этот файл в `/frame/native`. Два неизменных перехода намеренно различаются.
Host-only `BwSession` использует 400-байтовую 1-bpp shadow окна, base_id,
pending/success/invalidate, отвергает stale state и красный/жёлтый внутри.
Это подготовка к будущему подтверждённому загрузчику. Аппаратной B/W-сессии и
endpoint сейчас нет, ожидаемое состояние через hibernate/reset не переносится.

`measure` по умолчанию выполняет GET capabilities, **штатное full восстановление
A**, один выбранный A→B, сохраняет результат и останавливается для осмотра.
Нет cleanup full после удачи, redirect-following или повторов POST.
При transport timeout/обрыве исход помечается unknown и дальнейшие операции
не выполняются. При HTTP error серия также останавливается.
Инструмент требует наблюдённый BUSY также от штатного baseline/control:
без этого он помечает аномалию и не переходит к следующей операции, даже
если совместимый штатный endpoint ответил HTTP 200.
`--dry-run` печатает план без сетевых подключений и без записи результата.
Только `--series --count N --interval SECONDS` включает конечные повторы того
же выбранного опыта, N<=5, interval>=1. Каждый повтор восстанавливает A.
Интервал выбирается оператором и не является обещанием безопасной непрерывной
эксплуатации; в первом исследовании используйте один опыт.

JSON содержит параметры, URL, пути/хеши кадров, raw responses, firmware identity,
исход и стадию сбоя; соседние `.txt` сохраняют исходные тела ответов.
`results.csv` содержит одну строку на попытку. `--notes` и `--temperature`
заполняются вручную, температура по умолчанию null/пустая, не придуманное измерение.
После осмотра можно дополнить JSON/CSV вручную, сохраняя raw response неизменным.

## Копируемая последовательность

Команды запускаются из корня GDP075FW1. Каждый опыт ниже выполняйте **отдельно**,
после осмотра предыдущего; не вставляйте всю последовательность как autoplay.
Перед каждым `measure` baseline восстанавливается автоматически.

### 1. Setup, host-тесты и application build

При отсутствии окружения:

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-build.txt
```

```sh
python3 scripts/run_tests.py --sanitize
python3 scripts/build_experiments.py
cat dist/lut-pll/BUILD.json
(cd dist/lut-pll && sha256sum -c SHA256SUMS)
```

Build script запускает `scripts/pio.sh -e gdp075fw1` с версией
`VERSION-pll.<SHA256 исходников:12>` и Git revision, проверяет ESP checksum/SHA,
размер и offset app0. Выход — **только application** `dist/lut-pll/firmware.bin`,
BUILD.json и SHA256SUMS. Старые пользовательские .bin/SHA256SUMS не заменяются.
Файл VERSION/платформа/разметка не меняются. Новых build environments нет.

### 2. Прошивка именно этого application

Это инструкция оператору; автоматически плата не прошивается.
Убедитесь, что выбран её UART и совместимая текущая разметка app0=0x10000,
size=0x140000. Закройте монитор UART перед записью.

```sh
export EPD_PORT='/dev/ttyUSB0'
.venv/bin/python .platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32 --port "$EPD_PORT" write_flash \
  0x10000 dist/lut-pll/firmware.bin
```

### 3. Создать картинки и задать IP

```sh
export EPD_URL='http://192.168.1.208'
export EPD_PATTERNS='/tmp/gdp075fw1-experiments'
export EPD_RESULTS='/tmp/gdp075fw1-results'
python3 scripts/epd_experiments.py generate "$EPD_PATTERNS"
```

### 4. Capabilities и dry-run

```sh
mkdir -p "$EPD_RESULTS"
curl --fail-with-body --max-time 300 "$EPD_URL/display/experiments" \
  -o "$EPD_RESULTS/capabilities.txt"
cat "$EPD_RESULTS/capabilities.txt"
python3 scripts/epd_experiments.py measure \
  --baseline "$EPD_PATTERNS/baseline.native" \
  --candidate "$EPD_PATTERNS/candidate.native" --pll 0x09 --dry-run
```

### 5. Контроль: прежний fast-full, исходный 0x08

```sh
python3 scripts/epd_experiments.py measure \
  --baseline "$EPD_PATTERNS/baseline.native" \
  --candidate "$EPD_PATTERNS/candidate.native" \
  --base fast-full --output "$EPD_RESULTS" --notes 'stock fast-full control'
```

### 6. Одиночные PLL-тесты

Сначала 0x09, Dyna=1, FR=1 (номинал 25):

```sh
python3 scripts/epd_experiments.py measure \
  --baseline "$EPD_PATTERNS/baseline.native" \
  --candidate "$EPD_PATTERNS/candidate.native" \
  --base fast-full --pll 0x09 --output "$EPD_RESULTS"
```

После оценки — 0x0A, Dyna=1, FR=2 (50):

```sh
python3 scripts/epd_experiments.py measure \
  --baseline "$EPD_PATTERNS/baseline.native" \
  --candidate "$EPD_PATTERNS/candidate.native" \
  --base fast-full --pll 0x0A --output "$EPD_RESULTS"
```

Затем сравнить с 0x02, Dyna=0, тот же FR=2:

```sh
python3 scripts/epd_experiments.py measure \
  --baseline "$EPD_PATTERNS/baseline.native" \
  --candidate "$EPD_PATTERNS/candidate.native" \
  --base fast-full --pll 0x02 --output "$EPD_RESULTS"
```

Только после оценки 0x02 отдельно 0x03 (Dyna=0, FR=3, 65):

```sh
python3 scripts/epd_experiments.py measure \
  --baseline "$EPD_PATTERNS/baseline.native" \
  --candidate "$EPD_PATTERNS/candidate.native" \
  --base fast-full --pll 0x03 --output "$EPD_RESULTS"
```

После оценки 0x03 отдельно 0x04 (Dyna=0, FR=4, 75):

```sh
python3 scripts/epd_experiments.py measure \
  --baseline "$EPD_PATTERNS/baseline.native" \
  --candidate "$EPD_PATTERNS/candidate.native" \
  --base fast-full --pll 0x04 --output "$EPD_RESULTS"
```

Эквивалент одного ручного curl-опыта, **с явным отдельным восстановлением A**:

```sh
curl --fail-with-body --max-time 300 \
  -F "frame=@$EPD_PATTERNS/baseline.native" "$EPD_URL/frame/native"
```

```sh
curl --fail-with-body --max-time 300 \
  -H 'X-EPD-Experimental: 1' \
  -F "frame=@$EPD_PATTERNS/candidate.native" \
  "$EPD_URL/frame/native/pll?base=fast-full&pll=0x09" \
  -o "$EPD_RESULTS/manual-pll09.txt"
```

Оконный пример (каждый раз восстанавливает A):

```sh
python3 scripts/epd_experiments.py measure \
  --baseline "$EPD_PATTERNS/baseline.native" \
  --candidate "$EPD_PATTERNS/candidate.native" \
  --base fast-window --window 200 120 40 20 --pll 0x09 --output "$EPD_RESULTS"
```

Дополнительные 85/100/120 Hz **не входят в начальную последовательность**.
Их API-примеры — `pll=0x05&extended=1`, `pll=0x06&extended=1`,
`pll=0x07&extended=1` с Dyna=0; инструмент требует отдельного `--extended`.
Это явный расширенный opt-in, не автоматический перебор.

### 7. LUT: ожидаемый отказ

Это проверка capability gate, не запуск B/W waveform. Ожидается HTTP 501,
`refresh_started=false`, `refresh_count=0`, `pll_written=unknown`,
`blocked_reason=JD79665AA register-LUT ... unavailable; LUT_EN stays 0`.
`curl --fail-with-body` завершится кодом 22 и сохранит тело ошибки.

```sh
curl --fail-with-body --max-time 300 \
  -H 'X-EPD-Experimental: 1' \
  -F "frame=@$EPD_PATTERNS/bw-after.native" \
  "$EPD_URL/frame/native/lut?profile=bw-short" \
  -o "$EPD_RESULTS/lut-blocked.txt"
cat "$EPD_RESULTS/lut-blocked.txt"
```

Успешной LUT baseline/BW-команды в этой версии **нет**. B/W картинки можно
исследовать обычными native/PLL операциями, но это прежняя заводская цветная
waveform, не подтверждённый differential B/W LUT.

### 8. Результаты и заметки

```sh
ls -l "$EPD_RESULTS"
cat "$EPD_RESULTS/results.csv"
```

Для ручных заметок будущего опыта, например после измерения термометром,
добавьте `--temperature 25.0 --notes 'описание ghosting/цветов/мигания'`.
Не подставляйте примерную температуру как измеренную. JSON/CSV сохраняются
автоматически и при сбое. При unknown не повторяйте запрос, пока состояние
платы не выяснено/восстановлено.

| Опыт | Dyna/FR | refresh_ms | total_ms | BUSY seen | Температура (вручную) | Цвет/ghosting | Вне окна | UART/фото |
|---|---|---|---|---|---|---|---|---|
| stock fast-full | 1/0 | | | | | | n/a | |
| PLL 0x09 | 1/1 | | | | | | n/a | |
| PLL 0x0A | 1/2 | | | | | | n/a | |
| PLL 0x02 | 0/2 | | | | | | n/a | |
| PLL 0x03 | 0/3 | | | | | | n/a | |
| PLL 0x04 | 0/4 | | | | | | n/a | |
| fast-window PLL 0x09, 40×20 | 1/1 | | | | | | | |

Пришлите после первого 0x09 его JSON и control JSON, UART от начала запроса
(`EPD cfg`, включая последние 0x30/0x12) до `DISPLAY HTTP`, фотографию до и
после, наблюдения о мигании/остатках и фактическую температуру, если измеряли.
Фото делайте **до** штатного восстановления, иначе оно скроет артефакты.

### 9. Возврат к штатному режиму и картинке

Только после осмотра/фото и когда текущая операция завершена:

```sh
curl --fail-with-body --max-time 300 -X POST \
  -H 'X-EPD-Experimental: 1' "$EPD_URL/display/recover"
```

```sh
curl --fail-with-body --max-time 300 \
  -F "frame=@$EPD_PATTERNS/baseline.native" "$EPD_URL/frame/native"
```

Вместо тестового baseline можно вернуть свой полный native файл. Reset и
обычное изображение — штатный путь восстановления настроек, не гарантия
исправления любого физического последствия опыта.

## Валидация этой реализации

Выполнены host-тесты с ASan/UBSan и PlatformIO application build в `gdp075fw1`.
Тестируются все 256 PLL bytes с двумя уровнями opt-in, все четыре base,
порядок PON/PLL/DTM, обычный запрос после опыта, отказ register LUT до SPI,
ошибки multipart, late fields, BUSY/timeout/отсутствие assert, sticky errors,
cleanup и белый recovery без лишнего refresh. Старые HTTP/TCP/status тесты
остаются в runner. Python тестирует encoder/stale state, manifest/hash,
previews, dry-run, один опыт, HTTP error/timeout без повторов/redirects.

Фактические итоговые размер/версия/hash — в `dist/lut-pll/BUILD.json`;
статические RAM/flash — в финальном отчёте сборки. Runtime heap, реальная
частота и новая скорость/качество/ресурс панели без платы не измерялись.

Итоговая локальная сборка: `0.1.0-pll.695145ed5c26`, Git `658b316dc6df`;
application 872368 байт, статическая RAM 49632/327680, код flash
865797/1310720 (66.1%). Прошли 9 C++ suites и 23 Python tests с ASan/UBSan
для C++ (LeakSanitizer отключён из-за terminal sandbox). Проверка ESP image,
встроенной версии и source fingerprint выполнена build script.
Это результаты компьютера, не замер heap/таймингов на ESP32.

Ранее сообщённые **пользователем**, не этим агентом, измерения:

| Режим | refresh_ms |
|---|---:|
| full | 22079–22082 |
| partial 400×240 | 22074 |
| fast-full | 12246 |
| fast-window 400×240 | 12234 |
| fast-window 40×20 | 12231 |

Пользователь видит обновление только прямоугольника в fast-window 400×240
без мигания/изменений снаружи и слабые артефакты fast-full. Это исходная точка
сравнения, не результат теста новой PLL-ветки.
