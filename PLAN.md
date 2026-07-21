## Вердикт

У imway уже не «заготовка HDR», а хорошая базовая архитектура:

```text
client image
  → decode transfer function
  → linear BT.2020, абсолютные nits, FP16
  → композиция поверхностей и UI
  → output transform
  → PQ/BT.2020/XR30
  → KMS
```

Это правильнее большинства небольших композиторов. Непрозрачный HDR10/PQ happy path работает хорошо. Но до полноценного production-grade color management ещё далеко: остаются дорогие промежуточные surface conversions, неполный набор форматов и transfer functions, а реальный KMS link нельзя везде проверить программно.

Моя приблизительная оценка:

| Область | Состояние |
|---|---:|
| Основная HDR-математика для opaque RGB | 8/10 |
| Внутреннее пространство композиции | 9/10 |
| Wayland color-management | 6/10 |
| Tone/gamut mapping | 7/10 |
| KMS metadata и физический сигнал | 7/10 |
| HDR screenshot/JXL | 8/10 |
| HDR/SDR interoperability | 4/10 |
| Общая production-готовность | около 5/10 |

## Что уже сделано хорошо

Внутренняя сцена — `R16G16B16A16_SFLOAT`, linear BT.2020, где `1.0 == 1 nit`: [renderer.cpp](/home/pg/monorepo/imway/renderer.cpp:184). Это очень хорошая нормализация. Она позволяет без двусмысленности смешивать SDR UI с HDR-контентом, сохранять абсолютную яркость и не привязывать сцену к конкретному монитору. Подобный подход согласуется с рекомендациями ITU по FP16 HDR-композиции и reference white около 203 nit: [ITU-R BT.2408](https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-8-2024-PDF-E.pdf).

PQ декодируется в абсолютные nits, SDR декодируется из sRGB, переводится BT.709 → BT.2020 и умножается на пользовательский SDR white. HDR-контент при изменении SDR white не меняет яркость — это правильно и уже проверяется тестом.

Финальный transform тоже концептуально правильный:

- HDR: linear BT.2020 nits → PQ → XR30.
- SDR: BT.2020 → BT.709 → нормализация относительно SDR white → sRGB.

Он находится в [output_transform.frag](/home/pg/monorepo/imway/output_transform.frag:29).

HDR scanout использует XR30, `BT2020_RGB` и `HDR_OUTPUT_METADATA`; hardware cursor в HDR заменяется software cursor: [device_kms.cpp](/home/pg/monorepo/imway/device_kms.cpp:1024).

Скриншотилка тоже сильная:

- завершённый scanout передаётся viewer’у как dma-buf;
- compositor переключается на новый scanout;
- нет обычного копирования полного 32-МиБ кадра;
- владение памятью корректно удерживается dma-buf fd;
- JXL сохраняется lossless, в PQ/BT.2100, с 16-битным контейнером для 10-битных данных: [main_screenshot.cpp](/home/pg/monorepo/imway/main_screenshot.cpp:257).

Viewer правильно просит `VK_COLOR_SPACE_HDR10_ST2084_EXT`. Для Wayland WSI Vulkan сам управляет `wp_color_management_surface_v1`, поэтому отдельная ручная реализация протокола viewer’ом не нужна: [Vulkan WSI specification](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html).

## Wayland-протокол всё ещё ограничен

Сейчас поддерживаются:

- sRGB;
- PQ;
- HLG;
- extended linear/scRGB, включая Windows-scRGB;
- BT.709/sRGB primaries;
- BT.2020 primaries;
- parametric descriptions;
- полноценные output/preferred image descriptions, 64-битные identities и change events;
- perceptual intent — номинально.

## ImGui и UI

Linear-light blend внутренней сцены физически корректен. Это хорошая политика, хотя KWin сознательно использует nonlinear blend для более привычного вида desktop UI.

Но screenshot viewer сейчас рисует ImGui непосредственно поверх PQ-encoded swapchain. Fixed-function blending происходит над PQ code values, а не linear light. Crop overlays и прозрачный UI там имеют неправильную яркость.

Viewer тоже должен иметь небольшой FP16 linear/nits intermediate:

```text
HDR image + SDR UI
→ linear blend
→ PQ encode
→ swapchain
```

## Screenshots

JXL-путь хороший, но стоит дополнить:

- `intensity_target`;
- `min_nits`;
- явной информацией о фактическом target volume;
- проверкой декодированных pixel values, а не только тегов.

PNG clipboard fallback сейчас плох: PQ XR30 просто ужимается до 8 бит и записывается без HDR metadata и без tone mapping. Обычный PNG viewer воспримет это как SDR и покажет неверно.

Правильно:

- JXL — HDR master;
- PNG clipboard — tone-mapped и gamut-mapped SDR sRGB с корректным profile/chunks;
- при желании отдельно добавить современный HDR PNG/cICP, но не использовать его как compatibility fallback.

## Night light и brightness

Night light сейчас — RGB-множители в linear BT.2020. Это не полноценная chromatic adaptation:

- меняется экспозиция;
- возможен gamut clipping;
- absolute HDR luminance дрейфует;
- не учитываются primaries реального output.

Нужен CAT16/Bradford-подобный transform в XYZ либо общий output color pipeline.

Настройка SDR white в nits математически реализована правильно, но физически эти nits не гарантированы: аппаратная яркость монитора меняется независимо. Для настоящего результата нужен output brightness/headroom policy и хотя бы пользовательская калибровка.

## Где сейчас SOTA

Наиболее полезные ориентиры:

- **KWin** — лучший desktop reference: per-output profiling, brightness/headroom, HDR-on-SDR, ICtCp tone mapping, color pipeline abstraction.
- **Gamescope** — gaming/HDR reference: HDR10/scRGB, inverse tone mapping, LUT, metadata, direct scanout: [Gamescope](https://github.com/ValveSoftware/gamescope).
- **Weston** — reference implementation Wayland-протоколов и KMS color pipeline.
- **DRM Color Pipeline API** — направление для аппаратного offload matrix/1D LUT/3D LUT с shader fallback: [kernel Color Pipeline API](https://docs.kernel.org/gpu/rfc/color_pipeline.html).

imway уже ближе к Gamescope/KWin по внутренней модели сцены, чем к простому «HDR-флагу на output». Отставание сейчас не в фундаменте, а в полноте color pipeline вокруг него.

## Рекомендуемый порядок доведения

1. Dithering, корректный night light, linear screenshot viewer и SDR PNG fallback.
2. Затем аппаратный DRM color pipeline и безопасный HDR direct scanout.

Профильные HDR-тесты пока не могут проверить реальный KMS link и фотометрическую корректность физического дисплея.
