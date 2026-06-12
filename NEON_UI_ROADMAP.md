# Obsidian Neon UI Roadmap

This build starts the visual overhaul: neon palette, glass panels, stained-glass linework, glowing controls, and a darker modern top bar.

## Implemented in this pass

- Added `NeonObsidianLookAndFeel` in `PluginEditor.h/.cpp`.
- Replaced the generic JUCE midnight skin with custom neon rotary knobs.
- Added glass button, combo box, toggle, and horizontal slider drawing.
- Added dark glass page backgrounds with faint stained-glass geometry.
- Added section-specific neon accent colours:
  - Osc A / LFO 1: cyan
  - Osc B / LFO 2: magenta
  - Filter / Matrix: lime
  - Env / Sub / Output: amber
  - FX: violet
- Reworked panel painting to use glowing glass cards instead of flat rectangles.
- Reworked the top bar into a glass header with neon title treatment.
- Reworked matrix page row backgrounds into alternating neon glass strips.

## Implemented in the layout pass (Serum-style single screen)

- Removed the tabbed UI and the auto-wrapping flow grid. Everything now lives
  on one fixed signal-flow screen: header / oscillators + filter /
  envelopes + LFOs + voice / mod matrix + FX rack.
- Added live wavetable displays for Osc A and Osc B (reads the real
  mipmapped frames at the current morph position, updates on table/morph
  change and after WAV import). (roadmap item 1, display half)
- Added visual ADSR displays for Env 1 and Env 2. (item 2, display half)
- Added LFO shape displays for LFO 1 and LFO 2.
- Added a stylised filter response display (model / cutoff / resonance).
- Removed text boxes under knobs — values pop up while dragging.
- Bipolar sliders (mod amounts, Env2>Cut) now fill from centre.
- FX rack: six powered mini-cards in a grid instead of a separate tab.
- Mod matrix: eight compact rows on the main screen.

## Next feature/UI passes

1. Make the wavetable displays editable (drawing, FFT bin editing, 3D view).
2. Make the ADSR displays editable (drag nodes).
3. Add visual LFO editors with editable curves and tempo grid.
4. Add macro controls and macro modulation destinations.
5. Add modulation rings around knobs.
6. Add drag-and-drop modulation source handles.
7. Add preset browser with tags, favourites, author, and category metadata.
8. Add FX card reorder handles and animated meters.
9. Add more oscillator warp modes: bend, fold, quantize, PWM, asymmetry, AM, RM, remap.
10. Add more filter models: notch, comb, formant, diode, dirty ladder.

## Design rule

Use Serum-like workflow principles, but keep Obsidian visually and structurally distinct: black volcanic glass, neon stained-glass accents, crystalline panels, and spectral modulation colours.
