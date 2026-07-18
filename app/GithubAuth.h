#pragma once

#include <mutex>
#include <string>
#include <thread>

namespace ffatmo::app {

enum class GithubAuthPhase { SignedOut, Checking, AwaitingUser, Authorized, Error };

struct GithubAuthState {
    GithubAuthPhase phase = GithubAuthPhase::SignedOut;
    std::string userCode;
    std::string verificationUrl = "https://github.com/login/device";
    std::string login;
    std::string displayName;
    std::string avatarUrl;
    std::string error;
};

class GithubAuth {
public:
    GithubAuth() = default;
    ~GithubAuth();
    GithubAuth(const GithubAuth&) = delete;
    GithubAuth& operator=(const GithubAuth&) = delete;

    void start();
    void begin(bool remember);
    void openVerificationPage() const;
    void signOut();
    GithubAuthState snapshot() const;

private:
    void authenticateDevice(bool remember);
    bool validateToken(const std::string& token, bool remember);
    void setState(GithubAuthState state);
    mutable std::mutex mutex_;
    GithubAuthState state_;
    std::thread worker_;
};

}  // namespace ffatmo::app
