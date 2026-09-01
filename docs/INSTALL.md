# Installing KloudVocalShift

Free, open source, no account, no installer, no phone-home.

## macOS

Unzip, then drag:

- `KloudVocalShift.vst3`  ->  `/Library/Audio/Plug-Ins/VST3`
- `KloudVocalShift.component`  ->  `/Library/Audio/Plug-Ins/Components`

(Or into `~/Library/Audio/Plug-Ins/...` for just your user account. If you do
not see `~/Library` in Finder, hold Option while opening the Go menu.)

Restart your DAW and rescan plugins.

### "KloudVocalShift.vst3 cannot be opened because the developer cannot be verified"

That is macOS Gatekeeper, and it is not a sign anything is wrong with the file:
it means this build is not signed with a paid Apple Developer certificate.

Clearing the quarantine flag is one command in Terminal:

```
xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/KloudVocalShift.vst3
xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/Components/KloudVocalShift.component
```

Then restart your DAW.

You should not run that command on software you do not trust. The source for
this build is at https://github.com/kevkloud/kloudvocalshift and every release
is built by GitHub Actions from a public commit, so you can check what went into
it.

## Windows

Unzip and drag `KloudVocalShift.vst3` into:

```
C:\Program Files\Common Files\VST3
```

Restart your DAW and rescan plugins.

## Uninstalling

Delete the files. Nothing else is written anywhere.

## Which format

| DAW | Use |
|---|---|
| Ableton Live | VST3 |
| Logic Pro | AU (Logic does not load VST3) |
| FL Studio, Reaper, Studio One, Bitwig, Cubase | VST3 |
| Pro Tools | neither yet -- AAX needs a paid Avid signature |

## Latency

This plugin has a large, honest latency -- 128 ms at its default window, 256 ms
on Long. Your DAW compensates for it automatically when you play back the
arrangement, so everything stays in time. It is **not** suitable for monitoring
yourself through while recording a take. Record dry, then put this on the
channel.
