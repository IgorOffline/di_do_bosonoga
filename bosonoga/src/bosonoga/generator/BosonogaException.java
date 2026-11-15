package bosonoga.generator;

public final class BosonogaException extends RuntimeException {
    public BosonogaException(String sourceName, int line, int column, String message) {
        super("%s:%d:%d: %s".formatted(sourceName, line, column, message));
    }
}
