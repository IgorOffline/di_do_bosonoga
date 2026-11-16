package bosonoga.generator;

public final class BosonogaException extends RuntimeException {
    public BosonogaException(String sourceName, int line, int column, String message) {
        super("%s:%d:%d: %s".formatted(sourceName, line, column, message));
    }

    // An error of a whole file, such as regina.ini, starts with its name alone.
    public BosonogaException(String message) {
        super(message);
    }
}
