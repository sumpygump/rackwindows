/***********************************************************************************************
Tape
-------
VCV Rack module based on Tape by Chris Johnson from Airwindows <www.airwindows.com>

Ported and designed by Jens Robert Janke 

Changes/Additions:
- mono (should probably be stereo or have separate stereo module)
- cv inputs for slam
- trimpot cv input

Some UI elements based on graphics from the Component Library by Wes Milholen. 

See ./LICENSE.md for all licenses
************************************************************************************************/

/*
    ISSUES:
    - signal clips in Rack 
        - running it through the Dual BSG module with -8/+8 or more seems fine
        - however, padding the signal in process() similarly after input und bringing it back up before output does not yield the same result
*/

#include "plugin.hpp"

struct Tape : Module {
    enum ParamIds {
        SLAM_PARAM,
        SLAM_TRIM_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        SLAM_CV_INPUT,
        IN_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        OUT_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    double iirMidRollerAL;
    double iirMidRollerBL;
    double iirHeadBumpAL;
    double iirHeadBumpBL;

    double iirMidRollerAR;
    double iirMidRollerBR;
    double iirHeadBumpAR;
    double iirHeadBumpBR;

    long double biquadAL[9];
    long double biquadBL[9];
    long double biquadCL[9];
    long double biquadDL[9];

    long double biquadAR[9];
    long double biquadBR[9];
    long double biquadCR[9];
    long double biquadDR[9];
    bool flip;

    long double lastSampleL;
    long double lastSampleR;

    uint32_t fpd;

    float A;

    Tape()
    {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(SLAM_PARAM, 0.f, 1.f, 0.5f, "Slam");
        configParam(SLAM_TRIM_PARAM, -1.f, 1.f, 0.f, "Slam Trim");

        A = 0.5;
        iirMidRollerAL = 0.0;
        iirMidRollerBL = 0.0;
        iirHeadBumpAL = 0.0;
        iirHeadBumpBL = 0.0;
        iirMidRollerAR = 0.0;
        iirMidRollerBR = 0.0;
        iirHeadBumpAR = 0.0;
        iirHeadBumpBR = 0.0;
        for (int x = 0; x < 9; x++) {
            biquadAL[x] = 0.0;
            biquadBL[x] = 0.0;
            biquadCL[x] = 0.0;
            biquadDL[x] = 0.0;
            biquadAR[x] = 0.0;
            biquadBR[x] = 0.0;
            biquadCR[x] = 0.0;
            biquadDR[x] = 0.0;
        }
        flip = false;
        lastSampleL = 0.0;
        lastSampleR = 0.0;
        fpd = 17;
        //this is reset: values being initialized only once. Startup values, whatever they are.
    }

    void process(const ProcessArgs& args) override
    {
        // params
        A = inputs[SLAM_CV_INPUT].getVoltage() * params[SLAM_TRIM_PARAM].getValue() / 10;
        A += params[SLAM_PARAM].getValue();
        A = clamp(A, 0.01f, 0.99f);

        // double gainCut = 0.00390625;
        // double gainBoost = 256.0;
        double gainCut = 0.0000152587890625;
        double gainBoost = 65536.0;
        // double gainCut = 1;
        // double gainBoost = 1;

        float in1 = inputs[IN_INPUT].getVoltage();
        float in2 = inputs[IN_INPUT].getVoltage();

        // pad gain massively to prevent distortion
        in1 *= gainCut;
        in2 *= gainCut;

        /* ------- section below is untouched Airwindows code until output (except for line 132, 133)------- */

        double overallscale = 1.0;
        overallscale /= 44100.0;
        overallscale *= args.sampleRate;

        double inputgain = pow(10.0, ((A - 0.5) * 24.0) / 20.0);
        double HeadBumpFreq = 0.12 / overallscale;
        double softness = 0.618033988749894848204586;
        double RollAmount = (1.0 - softness) / overallscale;
        //[0] is frequency: 0.000001 to 0.499999 is near-zero to near-Nyquist
        //[1] is resonance, 0.7071 is Butterworth. Also can't be zero
        biquadAL[0] = biquadBL[0] = biquadAR[0] = biquadBR[0] = 0.0072 / overallscale;
        biquadAL[1] = biquadBL[1] = biquadAR[1] = biquadBR[1] = 0.0009;
        double K = tan(M_PI * biquadBR[0]);
        double norm = 1.0 / (1.0 + K / biquadBR[1] + K * K);
        biquadAL[2] = biquadBL[2] = biquadAR[2] = biquadBR[2] = K / biquadBR[1] * norm;
        biquadAL[4] = biquadBL[4] = biquadAR[4] = biquadBR[4] = -biquadBR[2];
        biquadAL[5] = biquadBL[5] = biquadAR[5] = biquadBR[5] = 2.0 * (K * K - 1.0) * norm;
        biquadAL[6] = biquadBL[6] = biquadAR[6] = biquadBR[6] = (1.0 - K / biquadBR[1] + K * K) * norm;

        biquadCL[0] = biquadDL[0] = biquadCR[0] = biquadDR[0] = 0.032 / overallscale;
        biquadCL[1] = biquadDL[1] = biquadCR[1] = biquadDR[1] = 0.0007;
        K = tan(M_PI * biquadDR[0]);
        norm = 1.0 / (1.0 + K / biquadDR[1] + K * K);
        biquadCL[2] = biquadDL[2] = biquadCR[2] = biquadDR[2] = K / biquadDR[1] * norm;
        biquadCL[4] = biquadDL[4] = biquadCR[4] = biquadDR[4] = -biquadDR[2];
        biquadCL[5] = biquadDL[5] = biquadCR[5] = biquadDR[5] = 2.0 * (K * K - 1.0) * norm;
        biquadCL[6] = biquadDL[6] = biquadCR[6] = biquadDR[6] = (1.0 - K / biquadDR[1] + K * K) * norm;

        long double inputSampleL = in1;
        long double inputSampleR = in2;

        if (fabs(inputSampleL) < 1.18e-43)
            inputSampleL = fpd * 1.18e-43;
        if (fabs(inputSampleR) < 1.18e-43)
            inputSampleR = fpd * 1.18e-43;

        if (inputgain < 1.0) {
            inputSampleL *= inputgain;
            inputSampleR *= inputgain;
        } //gain cut before anything, even dry

        long double drySampleL = inputSampleL;
        long double drySampleR = inputSampleR;

        long double HighsSampleL = 0.0;
        long double HighsSampleR = 0.0;
        long double NonHighsSampleL = 0.0;
        long double NonHighsSampleR = 0.0;
        long double tempSample;

        if (flip) {
            iirMidRollerAL = (iirMidRollerAL * (1.0 - RollAmount)) + (inputSampleL * RollAmount);
            iirMidRollerAR = (iirMidRollerAR * (1.0 - RollAmount)) + (inputSampleR * RollAmount);
            HighsSampleL = inputSampleL - iirMidRollerAL;
            HighsSampleR = inputSampleR - iirMidRollerAR;
            NonHighsSampleL = iirMidRollerAL;
            NonHighsSampleR = iirMidRollerAR;

            iirHeadBumpAL += (inputSampleL * 0.05);
            iirHeadBumpAR += (inputSampleR * 0.05);
            iirHeadBumpAL -= (iirHeadBumpAL * iirHeadBumpAL * iirHeadBumpAL * HeadBumpFreq);
            iirHeadBumpAR -= (iirHeadBumpAR * iirHeadBumpAR * iirHeadBumpAR * HeadBumpFreq);
            iirHeadBumpAL = sin(iirHeadBumpAL);
            iirHeadBumpAR = sin(iirHeadBumpAR);

            tempSample = (iirHeadBumpAL * biquadAL[2]) + biquadAL[7];
            biquadAL[7] = (iirHeadBumpAL * biquadAL[3]) - (tempSample * biquadAL[5]) + biquadAL[8];
            biquadAL[8] = (iirHeadBumpAL * biquadAL[4]) - (tempSample * biquadAL[6]);
            iirHeadBumpAL = tempSample; //interleaved biquad
            if (iirHeadBumpAL > 1.0)
                iirHeadBumpAL = 1.0;
            if (iirHeadBumpAL < -1.0)
                iirHeadBumpAL = -1.0;
            iirHeadBumpAL = asin(iirHeadBumpAL);

            tempSample = (iirHeadBumpAR * biquadAR[2]) + biquadAR[7];
            biquadAR[7] = (iirHeadBumpAR * biquadAR[3]) - (tempSample * biquadAR[5]) + biquadAR[8];
            biquadAR[8] = (iirHeadBumpAR * biquadAR[4]) - (tempSample * biquadAR[6]);
            iirHeadBumpAR = tempSample; //interleaved biquad
            if (iirHeadBumpAR > 1.0)
                iirHeadBumpAR = 1.0;
            if (iirHeadBumpAR < -1.0)
                iirHeadBumpAR = -1.0;
            iirHeadBumpAR = asin(iirHeadBumpAR);

            inputSampleL = sin(inputSampleL);
            tempSample = (inputSampleL * biquadCL[2]) + biquadCL[7];
            biquadCL[7] = (inputSampleL * biquadCL[3]) - (tempSample * biquadCL[5]) + biquadCL[8];
            biquadCL[8] = (inputSampleL * biquadCL[4]) - (tempSample * biquadCL[6]);
            inputSampleL = tempSample; //interleaved biquad
            if (inputSampleL > 1.0)
                inputSampleL = 1.0;
            if (inputSampleL < -1.0)
                inputSampleL = -1.0;
            inputSampleL = asin(inputSampleL);

            inputSampleR = sin(inputSampleR);
            tempSample = (inputSampleR * biquadCR[2]) + biquadCR[7];
            biquadCR[7] = (inputSampleR * biquadCR[3]) - (tempSample * biquadCR[5]) + biquadCR[8];
            biquadCR[8] = (inputSampleR * biquadCR[4]) - (tempSample * biquadCR[6]);
            inputSampleR = tempSample; //interleaved biquad
            if (inputSampleR > 1.0)
                inputSampleR = 1.0;
            if (inputSampleR < -1.0)
                inputSampleR = -1.0;
            inputSampleR = asin(inputSampleR);
        } else {
            iirMidRollerBL = (iirMidRollerBL * (1.0 - RollAmount)) + (inputSampleL * RollAmount);
            iirMidRollerBR = (iirMidRollerBR * (1.0 - RollAmount)) + (inputSampleR * RollAmount);
            HighsSampleL = inputSampleL - iirMidRollerBL;
            HighsSampleR = inputSampleR - iirMidRollerBR;
            NonHighsSampleL = iirMidRollerBL;
            NonHighsSampleR = iirMidRollerBR;

            iirHeadBumpBL += (inputSampleL * 0.05);
            iirHeadBumpBR += (inputSampleR * 0.05);
            iirHeadBumpBL -= (iirHeadBumpBL * iirHeadBumpBL * iirHeadBumpBL * HeadBumpFreq);
            iirHeadBumpBR -= (iirHeadBumpBR * iirHeadBumpBR * iirHeadBumpBR * HeadBumpFreq);
            iirHeadBumpBL = sin(iirHeadBumpBL);
            iirHeadBumpBR = sin(iirHeadBumpBR);

            tempSample = (iirHeadBumpBL * biquadBL[2]) + biquadBL[7];
            biquadBL[7] = (iirHeadBumpBL * biquadBL[3]) - (tempSample * biquadBL[5]) + biquadBL[8];
            biquadBL[8] = (iirHeadBumpBL * biquadBL[4]) - (tempSample * biquadBL[6]);
            iirHeadBumpBL = tempSample; //interleaved biquad
            if (iirHeadBumpBL > 1.0)
                iirHeadBumpBL = 1.0;
            if (iirHeadBumpBL < -1.0)
                iirHeadBumpBL = -1.0;
            iirHeadBumpBL = asin(iirHeadBumpBL);

            tempSample = (iirHeadBumpBR * biquadBR[2]) + biquadBR[7];
            biquadBR[7] = (iirHeadBumpBR * biquadBR[3]) - (tempSample * biquadBR[5]) + biquadBR[8];
            biquadBR[8] = (iirHeadBumpBR * biquadBR[4]) - (tempSample * biquadBR[6]);
            iirHeadBumpBR = tempSample; //interleaved biquad
            if (iirHeadBumpBR > 1.0)
                iirHeadBumpBR = 1.0;
            if (iirHeadBumpBR < -1.0)
                iirHeadBumpBR = -1.0;
            iirHeadBumpBR = asin(iirHeadBumpBR);

            inputSampleL = sin(inputSampleL);
            tempSample = (inputSampleL * biquadDL[2]) + biquadDL[7];
            biquadDL[7] = (inputSampleL * biquadDL[3]) - (tempSample * biquadDL[5]) + biquadDL[8];
            biquadDL[8] = (inputSampleL * biquadDL[4]) - (tempSample * biquadDL[6]);
            inputSampleL = tempSample; //interleaved biquad
            if (inputSampleL > 1.0)
                inputSampleL = 1.0;
            if (inputSampleL < -1.0)
                inputSampleL = -1.0;
            inputSampleL = asin(inputSampleL);

            inputSampleR = sin(inputSampleR);
            tempSample = (inputSampleR * biquadDR[2]) + biquadDR[7];
            biquadDR[7] = (inputSampleR * biquadDR[3]) - (tempSample * biquadDR[5]) + biquadDR[8];
            biquadDR[8] = (inputSampleR * biquadDR[4]) - (tempSample * biquadDR[6]);
            inputSampleR = tempSample; //interleaved biquad
            if (inputSampleR > 1.0)
                inputSampleR = 1.0;
            if (inputSampleR < -1.0)
                inputSampleR = -1.0;
            inputSampleR = asin(inputSampleR);
        }
        flip = !flip;

        long double groundSampleL = drySampleL - inputSampleL; //set up UnBox
        long double groundSampleR = drySampleR - inputSampleR; //set up UnBox

        if (inputgain > 1.0) {
            inputSampleL *= inputgain;
            inputSampleR *= inputgain;
        } //gain boost inside UnBox: do not boost fringe audio

        long double applySoften = fabs(HighsSampleL) * 1.57079633;
        if (applySoften > 1.57079633)
            applySoften = 1.57079633;
        applySoften = 1 - cos(applySoften);
        if (HighsSampleL > 0)
            inputSampleL -= applySoften;
        if (HighsSampleL < 0)
            inputSampleL += applySoften;
        //apply Soften depending on polarity
        applySoften = fabs(HighsSampleR) * 1.57079633;
        if (applySoften > 1.57079633)
            applySoften = 1.57079633;
        applySoften = 1 - cos(applySoften);
        if (HighsSampleR > 0)
            inputSampleR -= applySoften;
        if (HighsSampleR < 0)
            inputSampleR += applySoften;
        //apply Soften depending on polarity

        if (inputSampleL > 1.2533141373155)
            inputSampleL = 1.2533141373155;
        if (inputSampleL < -1.2533141373155)
            inputSampleL = -1.2533141373155;
        //clip to 1.2533141373155 to reach maximum output
        inputSampleL = sin(inputSampleL * fabs(inputSampleL)) / ((inputSampleL == 0.0) ? 1 : fabs(inputSampleL));
        //Spiral, for cleanest most optimal tape effect
        if (inputSampleR > 1.2533141373155)
            inputSampleR = 1.2533141373155;
        if (inputSampleR < -1.2533141373155)
            inputSampleR = -1.2533141373155;
        //clip to 1.2533141373155 to reach maximum output
        inputSampleR = sin(inputSampleR * fabs(inputSampleR)) / ((inputSampleR == 0.0) ? 1 : fabs(inputSampleR));
        //Spiral, for cleanest most optimal tape effect

        double suppress = (1.0 - fabs(inputSampleL)) * 0.00013;
        if (iirHeadBumpAL > suppress)
            iirHeadBumpAL -= suppress;
        if (iirHeadBumpAL < -suppress)
            iirHeadBumpAL += suppress;
        if (iirHeadBumpBL > suppress)
            iirHeadBumpBL -= suppress;
        if (iirHeadBumpBL < -suppress)
            iirHeadBumpBL += suppress;
        //restrain resonant quality of head bump algorithm
        suppress = (1.0 - fabs(inputSampleR)) * 0.00013;
        if (iirHeadBumpAR > suppress)
            iirHeadBumpAR -= suppress;
        if (iirHeadBumpAR < -suppress)
            iirHeadBumpAR += suppress;
        if (iirHeadBumpBR > suppress)
            iirHeadBumpBR -= suppress;
        if (iirHeadBumpBR < -suppress)
            iirHeadBumpBR += suppress;
        //restrain resonant quality of head bump algorithm

        inputSampleL += groundSampleL; //apply UnBox processing
        inputSampleR += groundSampleR; //apply UnBox processing

        inputSampleL += ((iirHeadBumpAL + iirHeadBumpBL) * 0.1); //and head bump
        inputSampleR += ((iirHeadBumpAR + iirHeadBumpBR) * 0.1); //and head bump

        if (lastSampleL >= 0.99) {
            if (inputSampleL < 0.99)
                lastSampleL = ((0.99 * softness) + (inputSampleL * (1.0 - softness)));
            else
                lastSampleL = 0.99;
        }

        if (lastSampleL <= -0.99) {
            if (inputSampleL > -0.99)
                lastSampleL = ((-0.99 * softness) + (inputSampleL * (1.0 - softness)));
            else
                lastSampleL = -0.99;
        }

        if (inputSampleL > 0.99) {
            if (lastSampleL < 0.99)
                inputSampleL = ((0.99 * softness) + (lastSampleL * (1.0 - softness)));
            else
                inputSampleL = 0.99;
        }

        if (inputSampleL < -0.99) {
            if (lastSampleL > -0.99)
                inputSampleL = ((-0.99 * softness) + (lastSampleL * (1.0 - softness)));
            else
                inputSampleL = -0.99;
        }
        lastSampleL = inputSampleL; //end ADClip L

        if (lastSampleR >= 0.99) {
            if (inputSampleR < 0.99)
                lastSampleR = ((0.99 * softness) + (inputSampleR * (1.0 - softness)));
            else
                lastSampleR = 0.99;
        }

        if (lastSampleR <= -0.99) {
            if (inputSampleR > -0.99)
                lastSampleR = ((-0.99 * softness) + (inputSampleR * (1.0 - softness)));
            else
                lastSampleR = -0.99;
        }

        if (inputSampleR > 0.99) {
            if (lastSampleR < 0.99)
                inputSampleR = ((0.99 * softness) + (lastSampleR * (1.0 - softness)));
            else
                inputSampleR = 0.99;
        }

        if (inputSampleR < -0.99) {
            if (lastSampleR > -0.99)
                inputSampleR = ((-0.99 * softness) + (lastSampleR * (1.0 - softness)));
            else
                inputSampleR = -0.99;
        }
        lastSampleR = inputSampleR; //end ADClip R

        if (inputSampleL > 0.99)
            inputSampleL = 0.99;
        if (inputSampleL < -0.99)
            inputSampleL = -0.99;
        //final iron bar
        if (inputSampleR > 0.99)
            inputSampleR = 0.99;
        if (inputSampleR < -0.99)
            inputSampleR = -0.99;
        //final iron bar

        //begin 64 bit stereo floating point dither
        int expon;
        frexp((double)inputSampleL, &expon);
        fpd ^= fpd << 13;
        fpd ^= fpd >> 17;
        fpd ^= fpd << 5;
        inputSampleL += ((double(fpd) - uint32_t(0x7fffffff)) * 1.1e-44l * pow(2, expon + 62));
        frexp((double)inputSampleR, &expon);
        fpd ^= fpd << 13;
        fpd ^= fpd >> 17;
        fpd ^= fpd << 5;
        inputSampleR += ((double(fpd) - uint32_t(0x7fffffff)) * 1.1e-44l * pow(2, expon + 62));
        //end 64 bit stereo floating point dither

        /* ------- end of untouched section ------- */

        // bring gain back up
        inputSampleL *= gainBoost;
        inputSampleR *= gainBoost;

        // output
        outputs[OUT_OUTPUT].setVoltage(inputSampleL);
        // outputs[OUT_OUTPUT].setVoltage(tanhDriveSignal(inputSampleL * 0.111f, 0.95f) * 9.999f);
    }
};

struct TapeWidget : ModuleWidget {
    TapeWidget(Tape* module)
    {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/tape_dark.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH * 1.5, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH * 1.5, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<RwKnobMediumDark>(Vec(30.0, 65.0), module, Tape::SLAM_PARAM));
        addParam(createParamCentered<RwKnobTrimpot>(Vec(30.0, 120.0), module, Tape::SLAM_TRIM_PARAM));

        addInput(createInputCentered<RwPJ301MPortSilver>(Vec(30.0, 245.0), module, Tape::SLAM_CV_INPUT));
        addInput(createInputCentered<RwPJ301MPortSilver>(Vec(30.0, 285.0), module, Tape::IN_INPUT));

        addOutput(createOutputCentered<RwPJ301MPort>(Vec(30.0, 325.0), module, Tape::OUT_OUTPUT));
    }
};

Model* modelTape = createModel<Tape, TapeWidget>("tape");