#!/usr/bin/env python3
"""Writes the in-page game guide (dist/help.html) for the web build.

The game content comes from the desktop key guides in
~/Desktop/Games/Roguelikes/Docs (build-docs.py + guides.py), so both guides
stay in sync; only the saving and "playing in the browser" parts are
written here, because they differ on the web."""
import html, importlib.util, os, sys

DOCS = os.path.expanduser('~/Desktop/Games/Roguelikes/Docs')
PAGE = 'larn.html'

sys.path.insert(0, DOCS)
spec = importlib.util.spec_from_file_location('build_docs', os.path.join(DOCS, 'build-docs.py'))
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)
from guides import GUIDES   # noqa: E402

game = next(g for g in docs.GAMES if g['file'] == PAGE)
guide = dict(GUIDES[PAGE])
info = dict(game['info'])
kbd = docs.kbd
esc = html.escape

SAVING = '''<ul>
<li><strong>Saving is automatic.</strong> The game is stored in this browser (IndexedDB) when it starts, every two minutes while it waits for your next command, and whenever you switch to another tab or window. Reloading the page continues from there.</li>
<li><kbd>S</kbd> saves and ends the session, as in the original; reload the page (or press <em>Play again</em>) to continue.</li>
<li>When your character dies (or you quit with <kbd>Q</kbd>) the game is over and the save is deleted; the next start begins a new character.</li>
<li>Each browser keeps <strong>one game</strong>. <em>New game</em> deletes it and starts over.</li>
<li><em>Export save</em> downloads the save file; <em>Import save</em> loads one. Use them for a backup or to move a game to another browser or computer (the Mac version reads the same file).</li>
<li>Your name and options (<code>larnopts</code>), the window layout, zoom, the sound/music switches and the high scores are stored in the same browser storage.</li>
<li>Private/incognito windows and "clear site data" delete the stored game. Export first if it matters.</li>
</ul>'''

WEB = '''<ul>
<li><strong>Windows:</strong> the tiled map with Messages (with history) below it; Status and Inventory on the right. Shops, help, lists and menus pop up over the map.</li>
<li><strong>Resize windows</strong> by dragging the gaps between them; a text window's contents shrink to fit when it is too small. <em>Reset windows</em> puts everything back.</li>
<li><strong>Zoom:</strong> <em>Zoom −</em> / <em>Zoom +</em> change the size of the map tiles. The map does not have to fit: when it is bigger than its window it scrolls to keep you in view. Hover over a text window's title to show its <em>A−</em> / <em>A+</em> buttons.</li>
<li><strong>Sound</strong> and <strong>Music</strong> are off until you switch them on in the top bar. Sound effects are from the Dubtrain Angband Sound Pack; music plays in town.</li>
<li><strong>Keys:</strong> the arrow keys, the numeric keypad, <kbd>1</kbd>–<kbd>9</kbd> or <kbd>hjklyubn</kbd> move you; the capital letters run.</li>
<li>Browsers keep a few shortcuts for themselves (<kbd>Ctrl+W</kbd>, <kbd>Ctrl+T</kbd>, <kbd>Ctrl+N</kbd>, and <kbd>Cmd</kbd> shortcuts on a Mac), so those never reach the game.</li>
<li>If the game ever crashes, a message appears at the top; reload the page to continue from the last autosave.</li>
</ul>'''

KEY_HINTS = [
    ('?', 'In-game help: the list of all commands'),
    ('x', 'Auto-explore: walk to the nearest unexplored spot'),
    ('Enter', 'Menu of all commands'),
    ('i', 'Inventory with a cursor: letter = use, Enter = all actions'),
    ('<', 'Go up (walks to the nearest known staircase)'),
    ('>', 'Go down (walks to the nearest known staircase; in town: the dungeon)'),
    ('E', 'Enter a shop, the bank, the college or the dungeon'),
    ('S', 'Save and quit'),
]


def dl(items):
    return '<dl>' + ''.join(f'<dt>{kbd(k)}</dt><dd>{esc(d)}</dd>' for k, d in items) + '</dl>'


def section(anchor, title, body):
    return f'<h2 id="h-{anchor}">{esc(title)}</h2>{body}'


parts = []
toc = [('about', 'About the game'), ('keys', 'Keyboard controls'), ('saving', 'Saving your game'),
       ('tips', 'Tips'), ('guide', "New player's guide"), ('web', 'Playing in the browser')]
parts.append('<p>' + esc(game['tagline']) + '</p>' + info['About the game'] + '<ul class="toc">' +
             ''.join(f'<li><a href="#h-{a}">{esc(t)}</a></li>' for a, t in toc) + '</ul>')

parts.append(section('about', 'About the game',
                     guide.pop('How Larn differs from Angband')))

ess = ''.join(f'<div class="box"><h3>{esc(cat)}</h3>{dl(items)}</div>' for cat, items in game['essentials'])
all_keys = game['all']() if callable(game['all']) else game['all']
full = ''.join(f'<div>{kbd(k)}<span>{esc(d)}</span></div>' for k, d in all_keys)
parts.append(section('keys', 'Keyboard controls',
                     '<div class="box key"><h3>The keys to remember</h3>' + dl(KEY_HINTS) + '</div>'
                     '<h3>Essential keys</h3><div class="grid">' + ess + '</div>'
                     '<details><summary>Complete key list (' + str(len(all_keys)) + ' commands)</summary>'
                     '<div class="all">' + full + '</div></details>'))

parts.append(section('saving', 'Saving your game', SAVING))
parts.append(section('tips', 'Tips', info['Tips']))
parts.append(section('guide', "New player's guide",
                     ''.join(f'<h3>{esc(t)}</h3>{b}' for t, b in guide.items())))
parts.append(section('web', 'Playing in the browser', WEB))

# RVIP: About this version (rogue2wasm.md: Source and changes)
parts.append('<h2 id="h-version">About this version</h2><ul>'
             '<li>Based on <strong>RL_M 26.4.0</strong> (Larn 12.x, maintained by Gibbon), commit 12cefcd.</li>'
             '<li>Original source: <a href="https://github.com/atsb/RL_M/tree/12cefcd" target="_blank" rel="noopener">atsb/RL_M, commit 12cefcd</a> (The Noah Licence: free, non-commercial).</li>'
             '<li>Our changes (port, auto-explore, command menu, inventory, web build): '
             '<a href="https://github.com/memmaker/larn/compare/12cefcd...master" target="_blank" rel="noopener">memmaker/larn</a></li>'
             '<li>Tiles: the Amiga Larn set from <a href="https://larn.org/" target="_blank" rel="noopener">larn.org</a> (github.com/primeau/Larn, MIT).</li></ul>')
print('\n'.join(parts))
