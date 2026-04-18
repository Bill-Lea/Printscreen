struct Cancelled final : std::exception {
    const char* what() const noexcept override { return "Cancelled"; }
};

inline void CancelIfRequested(std::atomic<bool>* flag, const char* where) {
    if (flag && flag->load(std::memory_order_relaxed)) {
        logger::info("Cancellation requested during: {}", where);
        throw Cancelled{};
    }
}
