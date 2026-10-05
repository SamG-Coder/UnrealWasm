#pragma once
// Metres, Unreal's Z-up axes. Twelve floats per box: center, size, color, roughness/metal/reserved.
static const float SampleBoxes[] = {
 0,0,-.15f, 12,12,.3f, .28f,.32f,.38f, .65f,0,0,
 0,4,2.5f, 12,.3f,5, .22f,.25f,.3f, .75f,0,0,
 -5,0,2.5f, .3f,8,5, .10f,.22f,.25f, .65f,0,0,
 0,0,.3f, 6,4,.6f, .09f,.11f,.15f, .35f,0,0,
 -2,0,1.5f, 1.2f,1.2f,2.4f, .8f,.23f,.06f, .25f,.2f,0,
 0,0,1.9f, 1.2f,1.2f,3.2f, .12f,.45f,.60f, .2f,.6f,0,
 2,0,1.1f, 1.2f,1.2f,1.6f, .7f,.72f,.75f, .15f,.9f,0,
 -2,2,1, 1,1,2, .38f,.4f,.44f, .7f,0,0,
 0,2,.6f, 1,1,1.2f, .65f,.55f,.3f, .3f,.6f,0,
 2,2,1.8f, 1,1,3.6f, .22f,.30f,.38f, .6f,0,0,
 3.7f,-1,.6f, .8f,.8f,1.2f, .45f,.16f,.1f, .5f,0,0,
 -3.5f,-1,.4f, .8f,.8f,.8f, .12f,.45f,.32f, .5f,0,0
};
static const int SampleBoxCount = sizeof(SampleBoxes)/sizeof(float)/12;
static const float SampleCamera[] = {9,-12,7, 0,0,1.2f, 60};
static const float SampleLight[] = {1,-3,6};
