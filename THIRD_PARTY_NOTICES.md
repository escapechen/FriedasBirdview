# Third-Party Notices

## Frigate

FriedasBirdview uses Frigate’s HTTP and live-stream interfaces. Its MSE
playback handling is adapted for compatibility with the Frigate project:

- Project: <https://github.com/blakeblackshear/frigate>
- License: MIT
- Copyright: Copyright (c) 2020 Blake Blackshear
- Additional notice retained from the Frigate source used for compatibility:
  Copyright (c) 2026 Frigate, Inc. (Frigate™)

The following MIT License applies to the Frigate-derived portions:

```text
The MIT License

Copyright (c) 2020 Blake Blackshear

Copyright (c) 2026 Frigate, Inc. (Frigate™)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## OpenSSL

FriedasBirdview uses OpenSSL’s X.509 API to verify that a certificate selected
in the custom-CA picker is a certificate authority before trusting it.

- Project: <https://www.openssl.org/>
- License: Apache License 2.0
- License text: <https://www.apache.org/licenses/LICENSE-2.0>
