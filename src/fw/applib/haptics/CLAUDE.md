# Haptics

Read this before adding, tuning, or calling a haptic. The numbers in `stone_haptics.c` are not
preferences; each one follows from something about the hardware, and changing one without knowing
which is how a system ends up feeling arbitrary.

## The one fact everything follows from

**There is a single LRA, mounted in one place.** The whole case moves together. No amount of
software can make a buzz happen in a corner, under a button, or on one side. If someone asks for
"haptics in the top right", the answer is not a driver change — it is this file.

What one actuator *can* vary is a feeling over time, and time is enough to encode direction. An
envelope that swells reads as arriving; one that fades reads as leaving. It is an illusion, and a
reliable one, but only while the whole system uses it the same way round. That is why
`StoneHapticMotion` is an enum and not a number.

## How to call it

Use a named effect. `stone_haptics_play(StoneHaptic_Select)`.

Named effects survive a retune; a hand-picked envelope does not. When the watch needs to feel
firmer, one table changes and everything moves together — which is exactly what does not happen
in a system where each call site chose its own milliseconds.

Reach for `stone_haptics_emit(strength, character, motion)` only when the named set genuinely
does not cover the meaning. If you find yourself calling it twice with the same arguments, add a
named effect instead.

Never call `vibes_*` directly for UI feedback. It bypasses this vocabulary, and the boolean
variants additionally scale by the wearer's *alert* intensity preference — which is about how
loud notifications are and has nothing to say about how a button should feel.

## Four things that will bite you

1. **There is a perceptibility floor.** A pulse that is both short and soft is felt as *nothing*,
   because it sits under the motor's start-up threshold. 25 ms at 25 % is the measured whisper on
   flat hardware. Going lighter does not make a subtler tick; it makes silence. If you are asked
   to make something "barely there", you are already at the bottom.

2. **Only a zero-amplitude segment is expensive.** `services/vibe_pattern/service.c` calls
   `vibe_ctl(false)` only for a zero step, and only that path pays the driver's stop-poll — up to
   80 ms of `psleep` on the timer task. A change between two *non-zero* amplitudes is a single
   gain write. This is the entire reason shaped envelopes are affordable, and why no envelope
   here returns to zero in the middle. **Do not add a silent gap inside an effect.**

3. **The motor does not stop mid-envelope, so the floor applies to the total.** A three-segment
   30 ms effect is 30 ms of continuous vibration, not three 10 ms pulses that would each be
   imperceptible. This is why per-segment durations here look shorter than the floor.

4. **The pattern service silently drops anything enqueued while a pattern plays.** An unthrottled
   caller does not get dense feedback; it gets *intermittent* feedback, which is worse than none.
   `stone_haptics_play` throttles light effects for this reason. Do not remove that without
   replacing it.

## Where haptics currently fire

- The three right-hand buttons, on press, from `kernel/event_loop.c`. Up rises, Select stays
  level, Down falls, so pressing them in sequence feels like travelling down the side of the
  watch.
- The back gesture, on commit, from `applib/ui/recognizer/touch_nav.c`.
- The watchface picker: paging and committing.
- Settings → Apps: picking a row up, moving it, dropping it, and refusing to move it out of its
  section.

## TODO

Deliberately not done yet, with the reasoning so it is not re-litigated.

- [ ] **Route the rest of the firmware's UI haptics through this.** `MenuLayer`'s scroll tick,
      `action_toggle`, and the dialogs all hand-roll their own patterns. The blocker is not
      effort: `vibes_enqueue_custom_pattern` is shared with genuinely alert-shaped callers
      (battery warnings, Bluetooth disconnect, the hourly chime), and rerouting the funnel
      wholesale would silently drop the wearer's Settings → Vibrations intensity preference for
      those. Do it call site by call site, UI ones only.
- [ ] **Back button haptic.** Currently silent, because the directional set is the three
      right-hand buttons. If it should buzz, it wants `Soft`/`Falling` — softer and quieter than
      the right-hand three, so the side you pressed is identifiable without looking.
- [ ] **Turn on VBAT compensation for RAM playback.** `SYSCTRL1_VBAT_MODE_EN` is written only
      inside `prv_config_cont_mode`, which runs only after calibration — so normal playback has
      no battery compensation and **haptics get quieter as the battery drains**. One bit, in
      `vibe_aw86225.c`.
- [ ] **Stop rewriting the mode on every start.** `vibe_ctl(true)` re-runs the whole seven-register
      configuration each time: 11 I²C transactions per vibration, 10 of them unchanged since
      init. Caching the mode would cut start latency to a single transaction.
- [ ] **Use more than one of the chip's eight sequencer slots.** The AW86225 can chain eight RAM
      waveforms with per-slot loop counts and play the lot from one GO, with no I²C traffic and no
      timer callbacks during playback. The driver uses slot 1, looped forever, gated by
      start/stop from software. This is where the 1 ms service granularity stops mattering.
- [ ] **RTP mode.** The largest gap by far: arbitrary 8-bit samples streamed over I²C, which is
      what turns gain-shaped envelopes into real waveform synthesis with attack transients.
      `PLAY_MODE_RTP` is not even defined. Check the schematic first — the board exposes no
      interrupt line for the AW86225, and FIFO refill wants one.
- [ ] **`vibe_get_braking_strength()` is wrong.** It reads `CONTCFG7`, a CONT-mode drive level,
      while playback runs in RAM mode. Every `.vibe` resource's `brake_duration_ms` therefore
      plays as a second forward vibration at an unrelated amplitude, rather than as braking.
