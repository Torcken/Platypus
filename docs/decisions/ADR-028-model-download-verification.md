# ADR-028: Verified Model Downloads

## Context

Phase 4 model files can be several gigabytes. The application must resume interrupted transfers,
bound disk use, and never expose an artifact as installed until its catalog hash is verified.

## Decision

Use libcurl with HTTPS-only redirects, a 100 GiB hard size ceiling, bounded progress callbacks,
cancellation, and a sibling `.part` file for resume. Accept only immutable Hugging Face resolve URLs
and require range responses for resumed transfers. Verify exact catalog size and SHA-256 with
OpenSSL Crypto, then create the final path without replacing an existing file.

## Consequences

OpenSSL Crypto is a build dependency. Tests inject a transfer function and do not use the network.
The download service is not yet wired into first-run setup; package and CI dependency provisioning
remain part of Phase 4 distribution work.
