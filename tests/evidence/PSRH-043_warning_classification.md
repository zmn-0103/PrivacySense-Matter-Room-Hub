# PSRH-043 Integrated Warning Classification

- Firmware code merge: c2a0ff09d70775a9d582bb3e8a71e455cfb49529
- Integration branch HEAD at build time:
  0d403f13ec0a0e4b2c32e16f35893f987606ae1d
- Date: 2026-08-09
- Target command: ninja -C /tmp/psrh-043-integrated-c2a0ff0.NTcPav -j2 all
- Result: exit 0
- Retained sanitized classification log:
  /home/administrator/Project/PrivacySense-Matter-Room-Hub-artifacts/psrh-043-phase5-integration/20260809/closeout/integrated-c2a0ff0/warning-classification.log
- Classification log SHA-256:
  496f164a30a7bf2cb029ba2c36db9c0f1753607b9aaa6f226a5ffaea15623d22
- Full baseline configure/build logs and integrated configure/build logs are
  retained and hashed in evidence-files.sha256.

## Project-owned warning result

The fresh 5044f9f baseline log contains this project-owned compiler warning:

~~~text
firmware/main/network.c:199:13: warning:
command_is_link_event defined but not used [-Wunused-function]
~~~

The integrated log contains no PSRH-043 project-owned compiler warning. The
unused command_is_link_event function was removed by the PSRH-043 integration,
and the warning is absent from the saved integrated build log.

The integrated project CMake file still emits a compatibility warning at its
required cmake_minimum_required(VERSION 3.5) boilerplate line. This warning
is explicitly classified as project-owned boilerplate/dependency compatibility:
the ESP-Matter example requires the directive in that position/order, and no
warning suppression was added.

The project-owned warning outcome is therefore:

- baseline unused-function warning: fixed and verified absent in integrated
  build;
- project CMake compatibility warning: retained and explicitly classified;
- compiler diagnostics: not suppressed.

## Upstream and dependency warnings retained

- ConnectedHomeIP/ESP-Matter camera optional-setting maybe-uninitialized
  diagnostics remain visible;
- the upstream Color Control direction maybe-uninitialized diagnostic remains
  visible;
- ESP-Matter/ConnectedHomeIP duplicate SEC_CERT_DAC_PROVIDER choice/default
  diagnostics remain visible;
- ESP-IDF, ESP-Matter, and managed-component CMake compatibility/deprecation
  diagnostics remain visible.

These warnings are classified from the saved logs, not inferred from the
build exit code. The integrated target linked successfully with exit 0. The
Host build separately used -Wall -Wextra -Werror and completed with exit 0
without a warning.
