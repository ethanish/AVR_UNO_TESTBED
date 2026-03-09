# P3 Event Spec (ADC Sag Detection)

## Sampling and Filter
- Sampling: ADC0 at 1kHz (1ms tick)
- Filter: moving average window = 8 samples

## Hysteresis Thresholds
- `TH_LOW`: sag start threshold
- `TH_RECOVER`: sag end threshold
- Constraint: `TH_LOW < TH_RECOVER`

## State Rules
- Start event: `FILT_ADC <= TH_LOW` when inactive
- End event: `FILT_ADC >= TH_RECOVER` when active
- During active event, track minimum filtered ADC value

## Logged Event Fields
- `ID`
- `START_MS`, `END_MS`, `DURATION_MS`
- `MIN_ADC`, `START_ADC`, `END_ADC`

## Notes
- Event log is ring-buffer based (`EVENT_LOG_CAP=16`)
- New events overwrite oldest entries when full
