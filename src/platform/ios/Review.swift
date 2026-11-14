// The store review request. Its current API (StoreKit's AppStore,
// iOS 16 and later) is Swift only; SKStoreReviewController, which C and
// Objective-C could call, is deprecated. Called from IosSystem.mm.
import StoreKit
import UIKit

@_cdecl("pinball_request_review")
public func pinballRequestReview() {
  // The game calls this on the main thread, from its frame or its events.
  MainActor.assumeIsolated {
    let scenes = UIApplication.shared.connectedScenes
    let active = scenes.first { $0.activationState == .foregroundActive }
    guard let scene = (active ?? scenes.first) as? UIWindowScene else {
      return
    }
    // The system decides whether to show anything, and limits how often.
    AppStore.requestReview(in: scene)
  }
}
