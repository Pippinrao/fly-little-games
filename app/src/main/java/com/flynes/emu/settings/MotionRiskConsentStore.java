package com.flynes.emu.settings;

import com.flynes.emu.video.quality.CertifiedVideoConfiguration;
import com.flynes.emu.video.quality.EvidenceClockTrust;
import com.flynes.emu.video.quality.EvidenceInvalidReason;
import com.flynes.emu.video.quality.EvidenceValidityPolicy;
import com.flynes.emu.video.quality.MotionRiskConsent;
import com.flynes.emu.video.quality.TrustedEvidenceClockSnapshot;
import com.flynes.emu.video.quality.VideoConfigurationKey;

import java.util.Objects;

/** Revalidates the complete Motion evidence identity at click and use time. */
public final class MotionRiskConsentStore {
    public interface CurrentContextProvider {
        CurrentContext current(VideoConfigurationKey requestedConfiguration);
    }

    public interface Persistence {
        boolean compareAndSet(long expectedProfileGeneration, MotionRiskConsent consent);
    }

    public static final class CurrentContext {
        private final long profileGeneration;
        private final String profileId;
        private final CertifiedVideoConfiguration certificate;
        private final TrustedEvidenceClockSnapshot clock;

        public CurrentContext(long profileGeneration, String profileId,
                              CertifiedVideoConfiguration certificate,
                              TrustedEvidenceClockSnapshot clock) {
            this.profileGeneration = profileGeneration;
            this.profileId = Objects.requireNonNull(profileId, "profileId");
            this.certificate = Objects.requireNonNull(certificate, "certificate");
            this.clock = Objects.requireNonNull(clock, "clock");
        }

        public long profileGeneration() { return profileGeneration; }
        public String profileId() { return profileId; }
        public CertifiedVideoConfiguration certificate() { return certificate; }
        public TrustedEvidenceClockSnapshot clock() { return clock; }
    }

    public static final class DialogIdentity {
        private final String profileId;
        private final String configurationId;
        private final VideoConfigurationKey configuration;
        private final String manifestSha256;
        private final long certifiedAtEpochMs;
        private final long validUntilEpochMs;
        private final String buildImplementationHash;
        private final String algorithmImplementationHash;
        private final int riskCopyVersion;

        private DialogIdentity(String profileId, CertifiedVideoConfiguration certificate,
                               int riskCopyVersion) {
            this.profileId = Objects.requireNonNull(profileId, "profileId");
            this.configurationId = certificate.configurationId();
            this.configuration = certificate.key();
            this.manifestSha256 = certificate.evidenceManifestSha256();
            this.certifiedAtEpochMs = certificate.certifiedAtEpochMs();
            this.validUntilEpochMs = certificate.validUntilEpochMs();
            this.buildImplementationHash = certificate.buildImplementationHash();
            this.algorithmImplementationHash = certificate.algorithmImplementationHash();
            this.riskCopyVersion = riskCopyVersion;
        }

        public static DialogIdentity from(String profileId,
                                          CertifiedVideoConfiguration certificate,
                                          int riskCopyVersion) {
            return new DialogIdentity(profileId,
                    Objects.requireNonNull(certificate, "certificate"), riskCopyVersion);
        }
    }

    private final CurrentContextProvider contextProvider;
    private final Persistence persistence;
    private final int currentRiskCopyVersion;

    public MotionRiskConsentStore(CurrentContextProvider contextProvider,
                                  Persistence persistence,
                                  int currentRiskCopyVersion) {
        this.contextProvider = Objects.requireNonNull(contextProvider, "contextProvider");
        this.persistence = Objects.requireNonNull(persistence, "persistence");
        this.currentRiskCopyVersion = currentRiskCopyVersion;
    }

    public MotionRiskConsent accept(DialogIdentity shownIdentity) {
        Objects.requireNonNull(shownIdentity, "shownIdentity");
        CurrentContext current = contextProvider.current(shownIdentity.configuration);
        if (current == null || !matches(shownIdentity, current)
                || current.clock().trust() != EvidenceClockTrust.TRUSTED) {
            return null;
        }
        long evaluatedAt = current.clock().evaluatedAtEpochMs();
        if (EvidenceValidityPolicy.evaluate(current.certificate(), evaluatedAt)
                != EvidenceInvalidReason.NONE) {
            return null;
        }
        CertifiedVideoConfiguration certificate = current.certificate();
        MotionRiskConsent consent = new MotionRiskConsent(current.profileId(),
                certificate.configurationId(), certificate.key(),
                certificate.evidenceManifestSha256(), certificate.certifiedAtEpochMs(),
                certificate.validUntilEpochMs(), evaluatedAt,
                certificate.buildImplementationHash(),
                certificate.algorithmImplementationHash(), currentRiskCopyVersion, evaluatedAt);
        return persistence.compareAndSet(current.profileGeneration(), consent) ? consent : null;
    }

    public boolean isValid(MotionRiskConsent consent, CurrentContext current) {
        if (consent == null || current == null
                || current.clock().trust() != EvidenceClockTrust.TRUSTED
                || consent.riskCopyVersion() != currentRiskCopyVersion
                || !consent.profileId().equals(current.profileId())) {
            return false;
        }
        CertifiedVideoConfiguration certificate = current.certificate();
        long evaluatedAt = current.clock().evaluatedAtEpochMs();
        return consent.configurationId().equals(certificate.configurationId())
                && consent.configuration().equals(certificate.key())
                && consent.evidenceManifestSha256().equals(certificate.evidenceManifestSha256())
                && consent.evidenceCertifiedAtEpochMs() == certificate.certifiedAtEpochMs()
                && consent.evidenceValidUntilEpochMs() == certificate.validUntilEpochMs()
                && consent.buildImplementationHash().equals(
                certificate.buildImplementationHash())
                && consent.algorithmImplementationHash().equals(
                certificate.algorithmImplementationHash())
                && consent.evidenceEvaluatedAtEpochMs() == consent.acceptedAtEpochMs()
                && certificate.certifiedAtEpochMs() <= consent.acceptedAtEpochMs()
                && consent.acceptedAtEpochMs() <= evaluatedAt
                && evaluatedAt < certificate.validUntilEpochMs()
                && EvidenceValidityPolicy.evaluate(certificate, evaluatedAt)
                == EvidenceInvalidReason.NONE;
    }

    private boolean matches(DialogIdentity shown, CurrentContext current) {
        CertifiedVideoConfiguration certificate = current.certificate();
        return shown.riskCopyVersion == currentRiskCopyVersion
                && shown.profileId.equals(current.profileId())
                && shown.configurationId.equals(certificate.configurationId())
                && shown.configuration.equals(certificate.key())
                && shown.manifestSha256.equals(certificate.evidenceManifestSha256())
                && shown.certifiedAtEpochMs == certificate.certifiedAtEpochMs()
                && shown.validUntilEpochMs == certificate.validUntilEpochMs()
                && shown.buildImplementationHash.equals(certificate.buildImplementationHash())
                && shown.algorithmImplementationHash.equals(
                certificate.algorithmImplementationHash());
    }
}
