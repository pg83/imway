"$IMWAY_CLIENT"
expect_alive "compositor died on malformed wire message"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "compositor stopped serving healthy clients after wire error"
