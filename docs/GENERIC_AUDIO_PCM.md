# Generic shared-clock audio

`audio.pcm@1` represents one PCM device that can grant independent input/output streams. This is a generic contract: it does not expose codec addresses, I2S registers, board pins or product identifiers. A physical codec, USB interface or other implementation may provide it. Public `audio.input@1` and `audio.output@1` adapters retain the deployed app ABI and translate mono/stereo samples without device knowledge.

A full-duplex device may require equal input/output sample rates because both directions share its clock. The device refuses an incompatible rate before creating that stream; it never stops an existing stream to change clocks. Closing one direction leaves the other active. False open with a nonzero token means cleanup remains necessary. Adapters keep that token until close succeeds. No partial/failed output is replayed.

Output gain is linear digital attenuation, with unity at level==maximum, not the analog codec's maximum register value. Input level preserves the deployed mean-absolute-magnitude semantics. Transfers are bounded to 256 frames and no caller buffer is borrowed after return.

The integration test compiles actual input/output adapters and the Waveshare ES8311/ES7210 provider as separate translation units, then connects their public tables. It covers both open orders, sample-rate conflicts, simultaneous input/output, volume set before open, mono/stereo mapping, mute, retained cleanup and a fault affecting both directions. The test uses modeled hardware; physical audio quality and operation are not asserted.
