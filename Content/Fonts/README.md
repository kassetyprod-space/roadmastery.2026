# Шрифты

Свободные шрифты с кириллицей под лицензией SIL Open Font License 1.1 (можно использовать в коммерческой игре,
нельзя продавать сами шрифты отдельно). Тексты лицензий — рядом.

| Файл | Шрифт | Где используется |
|---|---|---|
| `GolosText-Regular.ttf`, `GolosText-SemiBold.ttf` | [Golos Text](https://github.com/googlefonts/golos-text) (Paratype) | меню, HUD, таблички |
| `Unbounded-Bold.ttf` | [Unbounded](https://github.com/googlefonts/unbounded) | заголовок, крупные цифры |

Статические начертания получены из вариативных файлов Google Fonts (`fonttools varLib.instancer`).
Игра грузит их напрямую из `Content/Fonts` (без .uasset), в сборку они попадают через
`DirectoriesToAlwaysStageAsNonUFS` в `Config/DefaultGame.ini`.
