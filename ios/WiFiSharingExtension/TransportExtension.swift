// SPDX-License-Identifier: MIT
import AccessorySetupKit
import AccessoryTransportExtension
import ExtensionFoundation
import Foundation
import OSLog

@available(iOS 26.2, *)
@main
struct TransportExtension: AccessoryTransportAppExtension {
    private let logger = Logger(subsystem: "com.eric3u.bajji", category: "TransportExtension")

    @AppExtensionPoint.Bind
    static var boundExtensionPoint: AppExtensionPoint {
        AppExtensionPoint.Identifier("com.apple.accessory-transport-extension")
    }

    func accept(sessionRequest: AccessoryTransportSession.Request) ->
        AccessoryTransportSession.Request.Decision {
        logger.info("accepting accessory transport session")
        return sessionRequest.accept { EventHandler(session: sessionRequest.session) }
    }

    final class EventHandler: AccessoryTransportSession.EventHandler {
        private let logger = Logger(subsystem: "com.eric3u.bajji", category: "TransportExtension")
        private let accessorySession = ASAccessorySession()
        // Retain the session for the extension lifetime, as Apple's
        // SharingWiFiNetworkCredentials/TransportExtension.swift sample does.
        private let transportSession: AccessoryTransportSession
        private var provider: WiFiSharingProvider?

        init(session: AccessoryTransportSession) {
            transportSession = session
            logger.info("transport event handler initialized; activating AccessorySetupKit session")
            accessorySession.activate(on: .main) { [weak self] event in
                guard let self else { return }
                let eventType = String(describing: event.eventType)
                self.logger.info("extension AccessorySetupKit event: type=\(eventType, privacy: .public) accessories=\(self.accessorySession.accessories.count)")
                guard event.eventType == .activated else { return }
                guard let accessory = self.accessorySession.accessories.first else {
                    self.logger.error("extension activated without an accessory")
                    return
                }
                self.logger.info("starting Wi-Fi sharing provider for paired accessory")
                self.provider = WiFiSharingProvider(accessory: accessory)
                self.provider?.start()
            }
        }

        func invalidationHandler(error: AccessoryTransportSession.Error?) {
            logger.error("transport session invalidation handler: \(String(describing: error), privacy: .public)")
            invalidate()
        }

        @available(iOS 26.5, *)
        func sessionInvalidated(error: AccessoryTransportSession.Error?) {
            logger.error("transport session invalidated: \(String(describing: error), privacy: .public)")
            invalidate()
        }

        @available(iOS 26.5, *)
        func messageReceived(_ message: TransportMessage,
                             completion: @escaping @Sendable (AccessoryMessage.Result) -> Void) {
            logger.warning("received unsupported accessory transport message")
            completion(.failure(.transportUnavailable))
        }

        private func invalidate() {
            logger.info("stopping Wi-Fi sharing provider and invalidating accessory session")
            accessorySession.invalidate()
            provider?.stop()
        }
    }
}
