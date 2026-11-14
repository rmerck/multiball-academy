# Privacy policy

Multiball Academy collects no data.

- The game has no accounts, no advertising, no analytics and no third-party services.
- It makes no network connections. The one exception is a link: if you tap the privacy policy link on the About screen, the game asks the system to open this page in your web browser. The browser makes that connection, not the game, and the game sends nothing with it.
- It asks for no permissions: no camera, microphone, location, motion, Bluetooth, contacts or photos.
- It does not track you and contains no tracking code.

## What is stored on your device

Two small files, kept in the game's own data directory and never sent anywhere:

- the high score table (`highscores.txt`), with the five best scores, the rank reached and the three initials entered for each;
- while a game is left unfinished, that game (`savedgame.bin`): the score, the ball, the state of the table and where the balls were, so that it can be continued. It is written when the game goes to the background or is quit with a game in progress, and deleted when that game is continued, a new one is started, or it ends. It holds nothing about you.

If the game finds the high score file damaged or not in the form it writes, it does not use it and does not delete it: it moves it aside in the same directory as `highscores.txt.bad` (or `highscores.txt.bad.1` to `highscores.txt.bad.9` if earlier ones are still there) and starts a new table. Those copies hold nothing but what the score file held. A saved game that is damaged, or was written by another version, is simply deleted.

- iPhone and iPad: inside the app's own storage, which other apps cannot read. If you back up your device with iCloud or a computer, the file is part of your own backup like the rest of the app's data.
- macOS: `~/Library/Application Support/multiball-academy/` (`highscores.txt`, `savedgame.bin`)
- Linux: `~/.local/share/multiball-academy/` (or under `XDG_DATA_HOME` if that is set)

## Deleting it

- iPhone and iPad: delete the app. Its storage, including the high score table, any copy set aside and any saved game, is removed with it.
- macOS and Linux: delete the files, and any `highscores.txt.bad` files beside them, or the whole `multiball-academy` directory named above. The game starts a new, empty table.

## Children

The game collects nothing from anyone, whatever their age.

## Changes

If this policy changes, the new text replaces this page, and the page's history in the repository shows what changed and when.

## Contact

Questions about this policy: open an issue at <https://github.com/rmerck/multiball-academy/issues>, or write to <dev@mrck.app>. See also [SUPPORT.md](SUPPORT.md).
