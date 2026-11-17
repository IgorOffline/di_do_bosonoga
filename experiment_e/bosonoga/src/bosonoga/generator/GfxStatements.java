package bosonoga.generator;

import java.util.ArrayList;
import java.util.List;
import java.util.function.BiFunction;
import java.util.function.Function;
import org.antlr.v4.runtime.Token;

final class GfxStatements {
    interface Reader {
        Expression read(BosonogaParser.ValueContext value, Kind kind, String context);
    }

    private final Reader reader;
    private final Function<BosonogaParser.ValueContext, Expression> textValue;
    private final Function<Token, Token> next;
    private final Function<Token, String> where;
    private final BiFunction<Token, String, BosonogaException> error;

    GfxStatements(Reader reader, Function<BosonogaParser.ValueContext, Expression> textValue,
                  Function<Token, Token> next, Function<Token, String> where,
                  BiFunction<Token, String, BosonogaException> error) {
        this.reader = reader;
        this.textValue = textValue;
        this.next = next;
        this.where = where;
        this.error = error;
    }

    String emit(BosonogaParser.GfxStatementContext context, boolean inFrame, boolean canAwait) {
        var start = context.graphics;
        var word = start.getText();
        var values = context.value();
        if (start.getType() == BosonogaParser.GFX_AWAIT_NEXT_FRAME) {
            if (!canAwait) throw error.apply(start, word + " can only be in gfx_main");
            surplus(values, 0);
            return "runtime::gfx::next_frame();";
        }
        if (!inFrame) throw error.apply(start, word + " can only be in gfx_frame");
        var arguments = new ArrayList<String>();
        var count = 0;
        var align = 0;
        if (start.getType() == BosonogaParser.GFX_DRAW_TEXT) {
            align = alignment(values, 0, List.of("left", "center", "right"),
                    "left, center or right", start, new int[]{1, 2, 4});
            align |= alignment(values, 1, List.of("top", "middle", "baseline", "bottom"),
                    "top, middle, baseline or bottom", start, new int[]{8, 16, 64, 32});
            count = 2;
        }
        var kinds = switch (start.getType()) {
            case BosonogaParser.GFX_CLEAR -> List.of(Kind.COLOR);
            case BosonogaParser.GFX_DRAW_RECTANGLE -> List.of(Kind.F32, Kind.F32, Kind.F32, Kind.F32, Kind.COLOR);
            case BosonogaParser.GFX_DRAW_HEX, BosonogaParser.GFX_DRAW_CIRCLE ->
                    List.of(Kind.F32, Kind.F32, Kind.F32, Kind.COLOR);
            case BosonogaParser.GFX_DRAW_ICON -> List.of(Kind.F32, Kind.F32, Kind.ICON, Kind.F32, Kind.COLOR);
            case BosonogaParser.GFX_DRAW_TEXT -> List.of(Kind.F32, Kind.F32, Kind.FONT, Kind.F32, Kind.COLOR);
            default -> throw new IllegalStateException(word);
        };
        for (var kind : kinds) {
            if (count >= values.size()) {
                var got = after(values, start);
                throw error.apply(got, "expected a value, got " + Tokens.shown(got));
            }
            arguments.add(reader.read(values.get(count++), kind, word).cpp());
        }
        if (start.getType() == BosonogaParser.GFX_DRAW_TEXT) {
            if (count == values.size()) {
                var got = after(values, start);
                throw error.apply(got, "expected a value to draw, got " + Tokens.shown(got));
            }
            var text = new StringBuilder("runtime::gfx::Text()");
            while (count < values.size()) {
                var value = values.get(count++);
                text.append(".part(").append(where.apply(value.getStart())).append(", ")
                        .append(textValue.apply(value).cpp()).append(')');
            }
            arguments.add("std::uint32_t{" + align + "}");
            arguments.add(text.append(".take()").toString());
        } else {
            surplus(values, count);
        }
        var method = switch (start.getType()) {
            case BosonogaParser.GFX_CLEAR -> "clear";
            case BosonogaParser.GFX_DRAW_TEXT -> "draw_text";
            case BosonogaParser.GFX_DRAW_ICON -> "draw_icon";
            default -> word.substring("gfx_draw_".length());
        };
        return "gfx_window.scene." + method + "(" + String.join(", ", arguments) + ");";
    }

    private Token after(List<BosonogaParser.ValueContext> values, Token start) {
        return next.apply(values.isEmpty() ? start : values.getLast().getStop());
    }

    private int alignment(List<BosonogaParser.ValueContext> values, int index, List<String> names,
                          String expected, Token start, int[] flags) {
        var token = index < values.size() ? values.get(index).getStart() : after(values, start);
        var found = names.indexOf(token.getText());
        if (found < 0) throw error.apply(token, "expected " + expected + ", got " + Tokens.shown(token));
        return flags[found];
    }

    private void surplus(List<BosonogaParser.ValueContext> values, int count) {
        if (count < values.size()) {
            var got = values.get(count).getStart();
            throw error.apply(got, "expected a statement, got " + Tokens.shown(got));
        }
    }
}
