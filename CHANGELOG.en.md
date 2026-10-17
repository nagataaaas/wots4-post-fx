# Changelog

[日本語](CHANGELOG.md)

## [v2026.10.14.3](https://github.com/nagataaaas/wots4-post-fx/releases/tag/v2026.10.14.3)

- Added a 1.5x SSAA choice (1.5 times the width and height, 2.25 samples). It is lighter than 2x, with a milder effect.
- Shortened the SSAA choices on the options page so that they fit the value column.

## [v2026.10.14.2](https://github.com/nagataaaas/wots4-post-fx/releases/tag/v2026.10.14.2)

- Added sharpening (AMD CAS). Choose from Off / Low / Normal / High.
- SMAA now uses the Ultra preset of the official implementation and also looks at depth differences, so it smooths the edges of objects whose colors are close to the background.
- Anti-aliasing now runs before motion blur, so it works on the edges of objects rather than on blur streaks.
- SSAA now averages its samples with more accurate colors.
- When SSAA differs from its current state, a notice below it says that a restart is needed.
- Anti-aliasing and SSAA no longer turn off while the game window is minimized or in the background. Only motion blur pauses.
- A failed SMAA setup no longer stops the other effects.
- `config.ini` choices are now written as names (`off`, `fxaa`, `smaa`, `2x`, `low`, `normal`, `high`) instead of numbers. Earlier settings written as numbers fall back to the defaults.
- The Mod now comes with a `config.ini` that has the defaults and comments.
- The Mod can now be installed from the ZIP on Releases.

## [v2026.10.14](https://github.com/nagataaaas/wots4-post-fx/releases/tag/v2026.10.14)

- Initial release
