# rini

A scrolling-tiling window system for 9front, in the style of the Linux
compositor [niri](https://github.com/YaLTeR/niri).

Windows are columns on an endless horizontal strip. Opening a window adds a
column without resizing the others, and the view scrolls to follow the focus.
Each workspace has its own strip and its own floating layer.

Like rio, rini serves each window its own `/dev/cons`, `/dev/mouse`,
`/dev/winname` and the rest, so ordinary programs run in it unchanged. It runs
at the top level or nested inside rio or another rini. Everything can be
configured at runtime through `/mnt/rini/ctl`.

## Build and install

On 9front:

	cd src
	mk install	# binary to $home/bin/$objtype, man page to /sys/man/1
	mk test

Then run `rini`, or put it in your profile in place of rio. See `man rini`
for options, key bindings and the ctl commands, and `examples/rinistart` for a
startup script.

## Tests

The layout, keyboard, terminal and ctl logic is plain C with no Plan 9
dependencies. `mk test` in `src` runs its golden tests on 9front; on Linux:

	make -C layout test

## Notes

Written with the help of an AI coding assistant.

## License

MIT, see [LICENSE](LICENSE).
