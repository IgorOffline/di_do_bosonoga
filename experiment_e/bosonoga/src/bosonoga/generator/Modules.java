package bosonoga.generator;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

// Where each name is seen. The program is scope 0, and each file read with mod
// has its own, inside the scope that read it. A name is private to its scope,
// unless it is pub, which shows it to the scope that read the module.
final class Modules {
    // Where a name is declared, and so where it is seen.
    record Home(int scope, boolean isPub) {
    }

    private record Scope(String path, int parent) {
    }

    private final List<Scope> scopes = new ArrayList<>();
    // Of the constants, functions and arrays. Variables keep their own.
    private final Map<String, Home> homes = new HashMap<>();
    private int current;

    Modules(String programPath) {
        scopes.add(new Scope(programPath, 0));
    }

    boolean isProgram() {
        return current == 0;
    }

    // Returns the scope to leave back to.
    int enter(String path) {
        var outer = current;
        scopes.add(new Scope(path, outer));
        current = scopes.size() - 1;
        return outer;
    }

    void leave(int outer) {
        current = outer;
    }

    Home here(boolean isPub) {
        return new Home(current, isPub);
    }

    void declare(String name, boolean isPub) {
        homes.put(name, here(isPub));
    }

    Home home(String name) {
        return homes.get(name);
    }

    boolean isVisible(Home home) {
        var target = home.isPub() ? scopes.get(home.scope()).parent() : home.scope();
        var at = current;
        while (at != target) {
            if (at == 0) {
                return false;
            }
            at = scopes.get(at).parent();
        }
        return true;
    }

    // Why a name of that home is not seen here, as the C bosonoga says it.
    Optional<String> hidden(Home home, String shown) {
        if (home == null || isVisible(home)) {
            return Optional.empty();
        }
        var declared = scopes.get(home.scope());
        if (!home.isPub()) {
            return Optional.of("'%s' is private to %s".formatted(shown, declared.path()));
        }
        return Optional.of("'%s' is pub in %s, so only %s, which reads it with mod, sees it"
                .formatted(shown, declared.path(), scopes.get(declared.parent()).path()));
    }
}
