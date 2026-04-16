# Technical Discovery: AWS Credential Expiration is Independent of Session Token

## Executive Summary

AWS SDKs treat credential **expiration** and **session token** as independent concepts. A credential provider can return credentials with only `AccessKeyId`, `SecretAccessKey`, and `Expiration` (without `SessionToken`), and the SDK will automatically refresh those credentials before they expire.

This discovery enables using short-lived IAM access keys (like those from NetApp StorageGrid) without the full implementation of STS-style session tokens.

---

## Background: The Misconception

The AWS Security Token Service (STS) API always returns four fields:
- `AccessKeyId`
- `SecretAccessKey`
- `SessionToken` (required)
- `Expiration` (required)

This led to the industry assumption that **session tokens are required for temporary/refreshable credentials** - i.e., expiration is tightly coupled to STS session tokens.

---

## The Discovery: Separation of Concerns

### What AWS Actually Contracts

The AWS `credential_process` configuration (AWS Config Reference: https://docs.aws.amazon.com/sdkref/latest/guide/feature-process-credentials.html) defines a different contract:

```json
{
  "Version": 1,
  "AccessKeyId": "an AWS access key",
  "SecretAccessKey": "your AWS secret access key",
  "SessionToken": "the AWS session token for temporary credentials",  // Optional
  "Expiration": "RFC3339 timestamp for when the credentials expire"   // Required for refresh
}
```

**Key insight**: `SessionToken` is **optional**. Only `Expiration` is needed to enable auto-refresh.

---

## The Architecture: Two Distinct Concepts

AWS SDKs consistently implement a **Provider pattern** that separates two concerns:

| Concept | Responsibility | Example |
|---------|----------------|---------|
| **Credential Identity** | What authenticates you (the "what") | AccessKeyId, SecretAccessKey, SessionToken |
| **Credential Expiration** | When to refresh (the "when") | Expiration timestamp + provider logic |

### Why This Matters

1. **Credentials are just data carriers** - They hold authentication material but contain no refresh logic
2. **Providers manage lifecycle** - They track expiration, detect when refresh is needed, and re-fetch credentials
3. **STS credentials are just one source** - They happen to include both identity (session token) and expiration, but these are conceptually independent

---

## Cross-SDK Consensus

All major AWS SDK implementations agree on this separation:

| SDK | Expiration Enables Refresh | Session Token Optional | Verified |
|-----|---------------------------|------------------------|---------|
| Python/boto3 | ✅ | ✅ | ✅ |
| Java v1 | ✅ | ✅ | ✅ |
| Java v2 | ✅ | ✅ | ✅ |
| Go | ✅ | ✅ | ✅ |
| JavaScript | ✅ | ✅ | -

**No official documentation explicitly states this** - but all five implementations consistently agree, making it a de facto standard. (See Appendix for evidence)

---

## The Rationale: Why This Design Makes Sense

### 1. Different Credential Sources Have Different Characteristics

| Source | Has Session Token | Has Expiration |
|--------|------------------|----------------|
| IAM User Keys | No | No |
| STS Temporary Credentials | Yes | Yes |
| SSO Tokens | Yes | Yes |
| EC2 Instance Metadata | Yes | Yes |

Credential providers should consider **all** of these sources, not just STS.

### 2. Session Token is an Authentication Detail, Not a Refresh Signal

- **Session token** proves "who you are" to AWS (temporary IAM role)
- **Expiration** tells the SDK "when to get new credentials"
- These are orthogonal concerns that happen to coincide in STS

### 3. The Provider Pattern Enables Extensibility

If expiration were tied to session tokens:
- Custom credential sources couldn't use auto-refresh
- The SDK would need to know about every credential source type
- The `credential_process` mechanism wouldn't work for non-STS sources

By separating concerns:
- Any provider can return credentials with `Expiration` → auto-refresh works
- Providers can choose what type of credential to return
- The SDK doesn't need to understand the credential semantics

---

## Use Case: StorageGrid Short-Lived Credentials

NetApp StorageGrid (or similar S3-compatible systems) can issue IAM-style access keys with expiration but without STS session tokens:

```json
{
  "Version": 1,
  "AccessKeyId": "SG_ACCESS_KEY_ID",
  "SecretAccessKey": "SG_SECRET_ACCESS_KEY",
  "Expiration": "2026-04-14T20:00:00Z"
}
```

Without this discovery, you might think:
- ❌ "These credentials can't be refreshed - no session token"
- ❌ "Need to implement custom refresh logic"
- ❌ "Must use AssuemRole or GetSessionToken for Temporary Token Credentials"

What you can actually do:
- ✅ Return this JSON from `credential_process`
- ✅ SDK auto-refreshes before expiration
- ✅ No STS, no session token required
- ✅ Works with any AWS SDK

---

## Conclusion

The AWS SDK credential architecture is built on a fundamental separation:

1. **Credential Identity** (what): AccessKeyId + SecretAccessKey (+ optional SessionToken)
2. **Credential Expiration** (when): Timestamp + Provider refresh logic

These concepts are **independently optional**. The `Expiration` field enables auto-refresh regardless of whether `SessionToken` is present.

This is not explicitly documented by AWS, but all five major SDK implementations consistently agree on this behavior. The `credential_process` mechanism was designed with this flexibility, making it possible to use short-lived credentials from any source - including S3-compatible storage systems like NetApp StorageGrid that don't issue STS-style session tokens.

---

# Appendix

---

## SDK Implementation Evidence

### Python (botocore) - credentials.py:1083-1130

**Source**: https://github.com/boto/botocore/blob/main/botocore/credentials.py

```python
# Line 1086-1091: Creates RefreshableCredentials as long as Expiration is present
# Note: SessionToken is NOT required for refresh logic
if creds_dict.get('expiry_time') is not None:
    return RefreshableCredentials.create_from_metadata(
        creds_dict,
        lambda: self._retrieve_credentials_using(credential_process),
        self.METHOD,
    )

# Line 1093-1099: Returns basic Credentials (no refresh) if no Expiration
return Credentials(
    access_key=creds_dict['access_key'],
    secret_key=creds_dict['secret_key'],
    token=creds_dict.get('token'),        # Optional!
    method=self.METHOD,
    account_id=creds_dict.get('account_id'),
)
```

```python
# Line 1127-1128: SessionToken is parsed as optional
'token': parsed.get('SessionToken'),      # Optional
'expiry_time': parsed.get('Expiration'),  # Only this enables refresh
```

### Java (AWS SDK for Java v1) - ProcessCredentialsProvider.java

**Source**: https://github.com/aws/aws-sdk-java/blob/master/aws-java-sdk-core/src/main/java/com/amazonaws/auth/ProcessCredentialsProvider.java

```java
// Lines 183-191: SessionToken decides credential TYPE, but expiration is tracked INDEPENDENTLY
String sessionToken = getText(credentialsJson, "SessionToken");
if (sessionToken != null) {
    return new BasicSessionCredentials(accessKeyId, secretAccessKey, sessionToken, ...);
} else {
    return new BasicAWSCredentials(accessKeyId, secretAccessKey, ...);  // No token, but can still have expiration
}
```

```java
// Lines 205-213: Expiration handling is COMPLETELY SEPARATE from session token
private DateTime credentialExpirationTime(JsonNode credentialsJson) {
    String expiration = getText(credentialsJson, "Expiration");
    if (expiration != null) {
        DateTime credentialExpiration = new DateTime(...);
        return credentialExpiration.minus(expirationBuffer);
    } else {
        return DateTime.now().plusYears(9999);  // Never expires
    }
}
```

### Java (AWS SDK for Java v2)

**Source**: https://github.com/aws/aws-sdk-java-v2/blob/64276ecfb6bbab0c2e0b93efae0821671367f6eb/core/auth/src/main/java/software/amazon/awssdk/auth/credentials/ProcessCredentialsProvider.java

The refresh lifecycle is managed by `ProcessCredentialsProvider` (lines 61-73) using `CachedSupplier`, NOT by the credential classes themselves.

```java
// Lines 126-143: refreshCredentials() - the actual refresh logic
private RefreshResult<AwsCredentials> refreshCredentials() {
    AwsCredentials credentials = credentials(credentialsJson);
    Instant credentialExpirationTime = credentialExpirationTime(credentialsJson);

    return RefreshResult.builder(credentials)
                        .staleTime(credentialExpirationTime)
                        .prefetchTime(credentialExpirationTime.minusMillis(credentialRefreshThreshold.toMillis()))
                        .build();
}
```

```java
// Lines 148-177: SessionToken determines credential TYPE, expiration is parsed separately
private AwsCredentials credentials(JsonNode credentialsJson) {
    String sessionToken = getText(credentialsJson, "SessionToken");
    // If SessionToken present → AwsSessionCredentials (with expiration)
    // If no SessionToken → AwsBasicCredentials (NO expiration field)
    return sessionToken != null ?
           AwsSessionCredentials.builder()...expirationTime(credentialExpirationTime(credentialsJson)).build() :
           AwsBasicCredentials.builder()...build();  // No expirationTime
}
```

```java
// Lines 182-193: credentialExpirationTime is independent of SessionToken
private Instant credentialExpirationTime(JsonNode credentialsJson) {
    String expiration = getText(credentialsJson, "Expiration");
    if (expiration != null) {
        return DateUtils.parseIso8601Date(expiration);
    } else {
        return Instant.MAX;  // Never expires
    }
}
```

**Key insight**: In Java v2, `AwsSessionCredentials.expirationTime()` is purely informational. The actual refresh timing is controlled by `ProcessCredentialsProvider` extracting `Expiration` from the JSON and setting it on `RefreshResult.staleTime()` - completely independent of whether the credential is `AwsBasicCredentials` or `AwsSessionCredentials`.

### Go (aws-sdk-go v1) - processcreds

**Source**: https://github.com/aws/aws-sdk-go/blob/main/aws/credentials/processcreds/provider.go

```go
type credentialProcessResponse struct {
    Version         int
    AccessKeyID     string `json:"AccessKeyId"`
    SecretAccessKey string
    SessionToken    string  // Optional - marked as optional in struct
    Expiration      *time.Time  // Optional - but enables refresh when present
}
```

From AWS Go SDK documentation: *"If the Expiration key isn't present in the tool's output, the SDK assumes that the credentials are long-term credentials that don't refresh."*

```go
// Lines 287-291: Provider manages refresh lifecycle via SetExpiration
// Handle expiration
p.staticCreds = resp.Expiration == nil
if resp.Expiration != nil {
    p.SetExpiration(*resp.Expiration, p.ExpiryWindow)
}
```

**Key insight**: The Go SDK uses `staticCreds` boolean to track whether credentials are static (no expiration) or dynamic. The `ProcessProvider` implements the `credentials.Provider` interface with `Retrieve()` and `IsExpired()` methods - it manages its own expiration state via `credentials.Expiry` embedding. SessionToken is just passed through as part of the credential value; the provider's refresh logic is purely based on `Expiration` presence.

### JavaScript (AWS SDK for JavaScript v3)

**Source**: https://github.com/aws/aws-sdk-js-v3/blob/main/packages/credential-provider-process/src/index.ts

```typescript
type AwsCredentialIdentity = {
  accessKeyId: string;
  secretAccessKey: string;
  sessionToken?: string;  // Optional
  expiration?: Date;      // Optional - but SDK auto-refreshes when present
};
```

From docs: *"When the resolved credential object includes an `expiration` date, the SDK automatically calls the provider function again when fewer than 5 minutes remain before expiry. If no `expiration` is set, the provider is called only once per client lifetime."*
