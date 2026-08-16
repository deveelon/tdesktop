# Telegram Desktop Local Admin

База: официальный Telegram Desktop `v7.0.9` (`a1e89e1`), Windows x64.

## Что добавлено

- `Ctrl+Shift+A` открывает локальную админ-панель.
- Режим «Оригинал» мгновенно отключает все локальные подмены.
- Редактор текста и времени одного выделенного сообщения.
- Произвольный штатный разделитель даты перед выбранной пачкой сообщений.
- Одноразовая замена чисел или текста в уже загруженных сообщениях открытого чата.
- Постоянные правила замены для загруженных и новых сообщений, а также для
  локализованных строк и числовых значений интерфейса.
- Удаление постоянных правил.

После изменения постоянных правил реактивные элементы интерфейса обновляются
сразу. Элементы, созданные как статический текст, получают замену при следующем
открытии соответствующего окна или раздела.

Постоянные правила находятся в `TelegramForcePortable\tdata\local-admin.json`.
Разовые подмены, изменённые сообщения, время и разделители даты существуют
только до закрытия программы. Ни одна функция не отправляет изменения на
сервер Telegram.

## Сборка Windows x64 Portable

Нужны Visual Studio 2026, Windows SDK `10.0.26100.0`, Python 3.10 и Git.
Команды выполняются в **x64 Native Tools Command Prompt**, инициализированном
с `-vcvars_ver=14.44`.

Если у вас архив только с изменёнными файлами, распакуйте его поверх чистого
исходного дерева `v7.0.9`. Альтернатива — применить патч из корня репозитория:

```powershell
git apply .\telegram-desktop-local-admin-v7.0.9.patch
git submodule update --init --recursive
```

Получите собственные `api_id` и `api_hash` на `my.telegram.org`, но не
публикуйте их и не добавляйте в исходники. В терминале задайте:

```powershell
$env:TDESKTOP_API_ID="ВАШ_API_ID"
$env:TDESKTOP_API_HASH="ВАШ_API_HASH"
```

Запустите:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-win64-portable.ps1
```

Скрипт отключает автообновление, собирает `Release`, создаёт папку
`TelegramForcePortable` и архив:

`dist-local-admin\Telegram-LocalAdmin-7.0.9-win64-portable.zip`

Для диагностической сборки используйте:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-win64-portable.ps1 -Configuration Debug
```

Неподписанный пользовательский `Telegram.exe` может вызвать предупреждение
Windows SmartScreen. Точный размер `.exe` и ZIP скрипт печатает после сборки.

## Сборка через GitHub Actions

Workflow `.github/workflows/local-admin-win64-portable.yml` запускается вручную
и использует Windows Server 2025 с Visual Studio 2026.

В `Settings → Secrets and variables → Actions` создайте два repository secret:

- `TDESKTOP_API_ID`
- `TDESKTOP_API_HASH`

После этого откройте `Actions → Local Admin Windows x64 Portable`, нажмите
`Run workflow` и скачайте одноимённый artifact. Секреты не попадают в ZIP.
