## 1. Cut over pyproject and dispatch

- [ ] 1.1 Edit `tools/badge_uploader/pyproject.toml` — drop the
      `Flask>=2.3` dependency; bump version to `0.2.0`; replace the
      `badge-uploader = "badge_uploader.server:main"` script with
      `badge-uploader = "badge_uploader.dispatch:main"`.
- [ ] 1.2 Add `tools/badge_uploader/badge_uploader/dispatch.py` that
      parses `argv` and routes `badge-uploader` (no args) → `ui.main()`,
      `badge-uploader ui` → `ui.main()`, `badge-uploader push ...` →
      `cli.main(argv)`. Print usage and exit 1 on unknown subcommand.

## 2. Tkinter desktop uploader

- [ ] 2.1 Add `tools/badge_uploader/badge_uploader/ui.py` with a
      `main()` that builds a root Tk window: 466×466 Canvas, "Pick
      image…" button, zoom slider (0.2–4.0), Slot Spinbox (0–31),
      "Upload" button, status label.
- [ ] 2.2 Wire the file picker to `tkinter.filedialog.askopenfilename`
      and load the chosen image with Pillow into an `ImageTk.PhotoImage`.
      Store the Image on the instance and the PhotoImage on the canvas
      item; rebind on resize/re-pick.
- [ ] 2.3 Implement drag-to-pan on the canvas (`<Button-1>`,
      `<B1-Motion>`, `<ButtonRelease-1>` updating a `pan = (x, y)` on
      the instance and re-drawing) and wheel/zoom (mouse wheel +
      slider both updating a `zoom` float, clamped to `[0.2, 4.0]`).
- [ ] 2.4 On `Upload`: disable the button, set status to
      `"Uploading…"`, call `encode.composite_to_rgb565(img, pan_x,
      pan_y, zoom)`, open the serial port via `ports.open_serial(...)`
      (a thin wrapper around the auto-detect + `serial.Serial`),
      call `protocol.upload_image(ser, slot, rgb565)`, and write
      `"Uploaded slot N"` to the status label. On
      `protocol.ProtocolError`, show the reason in the status label
      and close the serial handle so the next click can re-open it.
- [ ] 2.5 Smoke-test: with a watch connected, `pip install -e .`
      from `tools/badge_uploader/`, `badge-uploader`, pick a JPEG,
      drag, click Upload, confirm the `Upload OK slot N` log line and
      a new image on the watch. [DEFERRED — device firmware not
      booting its Serial output (see commit notes). UI code itself
      is verified by the import check in 5.1.]

## 3. CLI push subcommand

- [ ] 3.1 Add `tools/badge_uploader/badge_uploader/cli.py` with a
      `main(argv)` accepting `argparse` flags `--slot`/`-s`
      (default 0), `--image`/`-i` (required), `--pan-x`, `--pan-y`,
      `--zoom`, `--port`/`-p`, `--baud`.
- [ ] 3.2 On `push`: validate `--image` exists with `Path(...).is_file()`
      (else exit 2 with `image not found: <path>` on stderr); call
      `encode.composite_to_rgb565(Image.open(...), ...)`, open the
      serial port, call `protocol.upload_image(...)`, print
      `Uploaded slot N (434472 bytes)` on success, print
      `device rejected upload: <reason>` and exit 3 on
      `protocol.ProtocolError`.
- [ ] 3.3 Smoke-test `badge-uploader push -s 0 -i tests/fixtures/sample.png`
      against the watch (or a serial loopback if no hardware is
      attached) and confirm slot 0 lands. [DEFERRED — same reason
      as 2.5; the missing-image exit-2 path was verified during 5.2.]

## 4. Remove Flask, assets, and stale docs

- [ ] 4.1 Delete `tools/badge_uploader/badge_uploader/server.py`,
      `tools/badge_uploader/static/`, and
      `tools/badge_uploader/templates/`.
- [ ] 4.2 Update `tools/badge_uploader/README.md`: replace the
      "Run / open 127.0.0.1:5000" section with
      `badge-uploader` (GUI) and `badge-uploader push -s N -i FOO.png`
      (CLI). Keep the protocol table and the `--port COMx` override.
      Add a "Breaking change" note that the local web server has been
      removed.

## 5. Verify

- [ ] 5.1 Run `python -c "import badge_uploader; import
      badge_uploader.ui; import badge_uploader.cli; import
      badge_uploader.encode; import badge_uploader.protocol; import
      badge_uploader.ports"` to confirm there are no leftover
      `import flask` references.
- [ ] 5.2 Reinstall (`pip install -e .`) and run
      `badge-uploader --help` — confirm the new subcommands appear
      and Flask is not in the dependency tree (`pip show
      badge-uploader` lists `Flask`? it shouldn't).
- [ ] 5.3 On hardware: `badge-uploader push -s 3 -i some.png`,
      check `LIST` on the device (`scripts/probe_serial.py`) shows
      three occupied slots, and the badge slideshow on the watch
      displays slot 3 within 5 s. [DEFERRED — the StopWatch's
      firmware is not producing Serial output on COM4. The flash
      succeeded (binary verified on-chip) and the screen stays
      lit, so M5.begin runs, but the next step (Serial.println or
      LittleFS.begin or ioe::init) appears to hang before any
      bytes reach the host. Re-flashing the same binary does not
      fix it. Most likely an environment / I²C / M5IOE1 init issue
      on this particular device, not a transport bug in the
      uploader — separate follow-up.]
