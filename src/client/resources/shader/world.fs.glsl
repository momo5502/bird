#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D textureObj;
varying vec2 v_texcoords;
varying float v_alpha;
varying vec3 v_normal;
varying vec3 v_worldpos;

float rand(vec2 co)
{
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    if (v_alpha <= 0.0001)
    {
        discard;
    }

    if (v_alpha < 0.999)
    {
        float selector = 1.0 / v_alpha;

        vec2 seed = v_texcoords + gl_FragCoord.xy;

        /*int grouping = 2;
        vec2 seed = gl_FragCoord.xy;
        seed.x = float(int(seed.x) / grouping);
        seed.y = float(int(seed.y) / grouping);
        seed.x += float(int(v_texcoords.x) / grouping);
        seed.y += float(int(v_texcoords.y) / grouping);*/

        float sum = rand(seed) * selector;

        if (int(mod(sum, selector)) != 0)
        {
            discard;
        }
    }

    gl_FragColor = vec4(texture2D(textureObj, v_texcoords).rgb, 1.0);
}
