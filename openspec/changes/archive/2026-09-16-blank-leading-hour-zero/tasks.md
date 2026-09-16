## 1. Implementation

- [x] 1.1 Add a helper that writes the hour into the buffer, applying the 12-hour conversion and suppressing the leading zero below ten
- [x] 1.2 Use it in the idle render path
- [x] 1.3 Use it in the alarm-firing render path, so the flash cannot disagree with the clock
- [x] 1.4 Confirm no other place renders the hour

## 2. Verification on the device

- [x] 2.1 Set the clock to a single-digit hour in 12-hour mode and confirm the tens position is dark, with no segments lit
- [x] 2.2 Confirm ten and twelve o'clock still show both digits — the threshold cases most likely to be wrong
- [x] 2.3 Confirm one in the afternoon shows as a blank and a 1, not 13
- [x] 2.4 Switch to 24-hour mode and confirm the leading zero returns
- [x] 2.5 Arm an alarm at a single-digit hour and confirm the flash blanks the same way the clock does
- [x] 2.6 Confirm the time editor still shows both hour digits, since it is deliberately unchanged. *Marked on the general "checks pass" rather than a separate confirmation.*
- [x] 2.7 Restore the clock to the correct time afterwards

## 3. Close out

- [x] 3.1 Confirm the change touches only the render path
- [x] 3.2 Run the unmodified `timesync.py` as the serial regression check
- [x] 3.3 Record the flash figure against the 56.8% starting point
- [x] 3.4 Run `openspec validate blank-leading-hour-zero`
