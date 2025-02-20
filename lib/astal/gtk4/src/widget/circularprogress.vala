/**
 * A circular progress bar widget for GTK4.
 *
 * CircularProgressBar is a custom widget that displays progress in a circular format.
 * It supports various styling options including center filling, radius filling, and
 * customizable line properties.
 */
public class Astal.CircularProgressBar : Gtk.Widget, Gtk.Buildable {
    private Astal.Gizmo _progress_arc;
    private Astal.Gizmo _center_fill;
    private Astal.Gizmo _radius_fill;
    private Gtk.Widget _child;

    private int _line_width;
    private double _percentage;

    /**
     * Whether the center of the circle is filled.
     */
    public bool center_filled { get; set; }

    /**
     * Whether the radius area is filled.
     */
    public bool radius_filled { get; set; }

    /**
     * The width of the circle's radius line.
     *
     * The line width in pixels. If the value is 0, then the CircularProgress will be shown as a pie chart.
     */
    public int line_width {
        get { return _line_width; }
        set {
            if (value < 0) {
                _line_width = 0;
            } else {
                _line_width = value;
            }
        }
    }

    /**
     * The line cap style for the progress stroke.
     *
     * Check [[https://docs.gtk.org/gsk4/enum.LineCap.html]] for more information.
     */
    public Gsk.LineCap line_cap { get; set; }

    /**
     * The fill rule for the center fill area.
     *
     * Check [[https://docs.gtk.org/gsk4/enum.FillRule.html]] for more information.
     */
    public Gsk.FillRule fill_rule { get; set; }

    /**
     * The progress value between 0.0 and 1.0.
     *
     * Values outside [0.0, 1.0] will be clamped.
     */
    public double percentage {
        get { return _percentage; }
        set {
            if (_percentage != value) {
                if (value > 1.0) {
                    _percentage = 1.0;
                } else if (value < 0.0) {
                    _percentage = 0.0;
                } else {
                    _percentage = value;
                }
            }
        }
    }

    /**
     * The child widget contained within the circular progress.
     */
    public Gtk.Widget? child {
        get { return _child; }
        set {
            if (_child == value) {
                return;
            }

            if (_child != null) {
                _child.unparent();
            }

            _child = value;

            if (_child != null) {
                _child.set_parent(this);
                _child.notify.connect(() => {
                    queue_draw();
                });
            }
        }
    }

    /*
     * Implements Gtk.Buildable interface.
     */
    public void add_child(Gtk.Builder builder, GLib.Object child, string? type) {
        if (child is Gtk.Widget) {
            this.child = (Gtk.Widget)child;
        } else {
            base.add_child(builder, child, type);
        }
    }

    static construct {
        set_css_name("circularprogress");
        Gtk.CssProvider css_provider = new Gtk.CssProvider();
        css_provider.load_from_string("""
            circularprogress progress {
                color: @accent_color;
            }
            circularprogress radius {
                color: @headerbar_backdrop_color;
            }
            circularprogress center {
                color: @headerbar_shade_color;
            }
        """);

        Gtk.StyleContext.add_provider_for_display(
            Gdk.Display.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }

    construct {
        // Initialize progress arc gizmo
        _progress_arc = new Astal.Gizmo(
            "progress",
            (orientation, for_size, out minimum, out natural, out min_baseline, out nat_baseline) => {
            minimum = natural = get_width();
            min_baseline = nat_baseline = -1;
        },
            null,
            draw_progress_arc,
            null, null, null
        );

        // Initialize center fill gizmo
        _center_fill = new Astal.Gizmo(
            "center",
            (orientation, for_size, out minimum, out natural, out min_baseline, out nat_baseline) => {
            minimum = natural = get_width();
            min_baseline = nat_baseline = -1;
        },
            null,
            draw_center_fill,
            null, null, null
        );

        // Initialize radius fill gizmo
        _radius_fill = new Astal.Gizmo(
            "radius",
            (orientation, for_size, out minimum, out natural, out min_baseline, out nat_baseline) => {
            minimum = natural = get_width();
            min_baseline = nat_baseline = -1;
        },
            null,
            draw_radius_fill,
            null, null, null
        );

        _progress_arc.set_parent(this);
        _center_fill.set_parent(this);
        _radius_fill.set_parent(this);

        layout_manager = new Gtk.BinLayout();

        notify.connect(() => {
            queue_draw();
        });
    }

    public CircularProgressBar() {
        Object(
            name : "circularprogress"
        );
        notify.connect(() => {
            queue_draw();
        });
    }

    protected override void dispose() {
        if (_child != null) {
            _child.unparent();
            _child = null;
        }
        _progress_arc.unparent();
        _progress_arc = null;
        _center_fill.unparent();
        _center_fill = null;
        _radius_fill.unparent();
        _radius_fill = null;
        base.dispose();
    }

    public override void snapshot(Gtk.Snapshot snapshot) {
        var width = get_width();
        var height = get_height();
        var radius = float.min(width / 2.0f, height / 2.0f) - 1;

        // Adjust delta based on line width to prevent overflow
        float half_line_width = (float)line_width / 2.0f;
        var delta = radius - half_line_width;

        if (delta < 0) {
            delta = 0;
        }

        var actual_line_width = (float)line_width;
        if (actual_line_width > radius * 2) {
            actual_line_width = radius * 2;
        }

        // Draw in correct order: background to foreground
        if (center_filled) {
            _center_fill.snapshot(snapshot);
        }

        if (radius_filled) {
            _radius_fill.snapshot(snapshot);
        }

        _progress_arc.snapshot(snapshot);

        if (_child != null) {
            _child.snapshot(snapshot);
        }
    }

    private void draw_progress_arc(Gtk.Snapshot snapshot) {
        if (_percentage <= 0) {
            return;
        }

        var width = get_width();
        var height = get_height();
        var radius = float.min(width / 2.0f, height / 2.0f) - 1;
        var half_line_width = (float)line_width / 2.0f;
        var delta = radius - half_line_width;
        var center_x = width / 2.0f;
        var center_y = height / 2.0f;

        if (delta < 0) {
            delta = 0;
        }

        var actual_line_width = (float)line_width;
        if (actual_line_width > radius * 2) {
            actual_line_width = radius * 2;
        }

        var color = _progress_arc.get_color();
        var start_angle = 1.5f * Math.PI;
        var end_angle = start_angle + (_percentage * 2 * Math.PI);
        var path_builder = new Gsk.PathBuilder();

        // Draw as pie when line_width is 0
        if (_line_width <= 0) {
            path_builder.move_to(center_x, center_y);

            if (_percentage >= 1.0) {
                path_builder.add_circle(
                    Graphene.Point().init(center_x, center_y),
                    delta
                );
            } else {
                var start_x = center_x + (float)(delta * Math.cos(start_angle));
                var start_y = center_y + (float)(delta * Math.sin(start_angle));
                var end_x = center_x + (float)(delta * Math.cos(end_angle));
                var end_y = center_y + (float)(delta * Math.sin(end_angle));

                path_builder.line_to(start_x, start_y);
                path_builder.svg_arc_to(
                    delta, delta, 0.0f,
                    _percentage > 0.5, true,
                    end_x, end_y
                );
                path_builder.line_to(center_x, center_y);
                path_builder.close();
            }

            snapshot.append_fill(path_builder.to_path(), Gsk.FillRule.EVEN_ODD, color);
        } else {
            // Original stroke drawing code
            if (_percentage >= 1.0) {
                path_builder.add_circle(
                    Graphene.Point().init(center_x, center_y),
                    delta
                );
            } else {
                var start_x = center_x + (float)(delta * Math.cos(start_angle));
                var start_y = center_y + (float)(delta * Math.sin(start_angle));
                var end_x = center_x + (float)(delta * Math.cos(end_angle));
                var end_y = center_y + (float)(delta * Math.sin(end_angle));

                path_builder.move_to(start_x, start_y);
                path_builder.svg_arc_to(
                    delta, delta, 0.0f,
                    _percentage > 0.5, true,
                    end_x, end_y
                );
            }

            var stroke = new Gsk.Stroke(_line_width);
            stroke.set_line_cap(_line_cap);
            snapshot.append_stroke(path_builder.to_path(), stroke, color);
        }
    }

    private void draw_center_fill(Gtk.Snapshot snapshot) {
        if (!center_filled) {
            return;
        }

        var width = get_width();
        var height = get_height();
        var radius = float.min(width / 2.0f, height / 2.0f) - 1;
        var half_line_width = (float)line_width / 2.0f;
        var delta = radius - half_line_width;

        if (delta < 0) {
            delta = 0;
        }

        var color = _center_fill.get_color();
        var path_builder = new Gsk.PathBuilder();

        path_builder.add_circle(
            Graphene.Point().init(width / 2.0f, height / 2.0f),
            delta
        );

        snapshot.append_fill(
            path_builder.to_path(),
            fill_rule,
            color
        );
    }

    private void draw_radius_fill(Gtk.Snapshot snapshot) {
        if (!radius_filled) {
            return;
        }

        var width = get_width();
        var height = get_height();
        var radius = float.min(width / 2.0f, height / 2.0f) - 1;
        var half_line_width = (float)line_width / 2.0f;
        var delta = radius - half_line_width;
        var center_x = width / 2.0f;
        var center_y = height / 2.0f;

        if (delta < 0) {
            delta = 0;
        }

        var color = _radius_fill.get_color();
        var path_builder = new Gsk.PathBuilder();

        if (_line_width <= 0) {
            path_builder.add_circle(
                Graphene.Point().init(center_x, center_y),
                delta
            );
            snapshot.append_fill(path_builder.to_path(), Gsk.FillRule.EVEN_ODD, color);
        } else {
            path_builder.add_circle(
                Graphene.Point().init(center_x, center_y),
                delta
            );
            var stroke = new Gsk.Stroke(_line_width);
            snapshot.append_stroke(path_builder.to_path(), stroke, color);
        }
    }
}
