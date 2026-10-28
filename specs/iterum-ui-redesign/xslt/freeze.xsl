<?xml version="1.0" encoding="UTF-8"?>
<!--
  freeze.xsl - rebuilds template FreezePanel exactly as specs/iterum-ui-redesign/panels/freeze.md specifies.
  Identity transform for everything else.
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">
  <xsl:include href="cells.xsl"/>
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:template match="@*|node()">
    <xsl:copy><xsl:apply-templates select="@*|node()"/></xsl:copy>
  </xsl:template>

  <xsl:template match="template[@name='FreezePanel']">
    <template name="FreezePanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel"
              transparent="false">
      <!-- core 1: Slice Length knob, no sync -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'FreezeSliceLength'"/>
        <xsl:with-param name="label" select="'Slice Length'"/>
      </xsl:call-template>
      <!-- core 2: Pattern select -->
      <xsl:call-template name="select-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'FreezePatternType'"/>
        <xsl:with-param name="label" select="'Pattern'"/>
      </xsl:call-template>
      <!-- core 3: Mix knob -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'FreezeDryWet'"/>
        <xsl:with-param name="label" select="'Mix'"/>
      </xsl:call-template>

      <!-- SLICE -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'SLICE'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'FreezeSliceMode'"/>
            <xsl:with-param name="label" select="'Slice Mode'"/>
            <xsl:with-param name="segments" select="'Fixed,Variable'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- ENVELOPE -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="288"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'ENVELOPE'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'FreezeEnvelopeAttack'"/>
            <xsl:with-param name="label" select="'Attack'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'FreezeEnvelopeRelease'"/>
            <xsl:with-param name="label" select="'Release'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'FreezeEnvelopeShape'"/>
            <xsl:with-param name="label" select="'Envelope Shape'"/>
            <xsl:with-param name="segments" select="'Linear,Exponential'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- EUCLIDEAN (anchor FreezeEuclideanSteps first) -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="580"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'EUCLIDEAN'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeEuclideanSteps'"/>
            <xsl:with-param name="label" select="'Steps'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="302"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeEuclideanHits'"/>
            <xsl:with-param name="label" select="'Hits'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeEuclideanRotation'"/>
            <xsl:with-param name="label" select="'Rotation'"/>
          </xsl:call-template>
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="302"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezePatternRate'"/>
            <xsl:with-param name="label" select="'Pattern Rate'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- GRANULAR SCATTER -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="580"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'GRANULAR SCATTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeGranularDensity'"/>
            <xsl:with-param name="label" select="'Density'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="302"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeGranularGrainSize'"/>
            <xsl:with-param name="label" select="'Grain Size'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeGranularPositionJitter'"/>
            <xsl:with-param name="label" select="'Position Jitter'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="302"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeGranularSizeJitter'"/>
            <xsl:with-param name="label" select="'Size Jitter'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- HARMONIC DRONES -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="580"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'HARMONIC DRONES'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeDroneVoiceCount'"/>
            <xsl:with-param name="label" select="'Voices'"/>
          </xsl:call-template>
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="302"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeDroneInterval'"/>
            <xsl:with-param name="label" select="'Interval'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeDroneDrift'"/>
            <xsl:with-param name="label" select="'Drift'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="302"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeDroneDriftRate'"/>
            <xsl:with-param name="label" select="'Drift Rate'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- NOISE BURSTS -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="580"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'NOISE BURSTS'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeNoiseColor'"/>
            <xsl:with-param name="label" select="'Noise Color'"/>
          </xsl:call-template>
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="302"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeNoiseBurstRate'"/>
            <xsl:with-param name="label" select="'Burst Rate'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeNoiseFilterType'"/>
            <xsl:with-param name="label" select="'Filter Type'"/>
            <xsl:with-param name="segments" select="'LowPass,HighPass,BandPass'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="302"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeNoiseFilterCutoff'"/>
            <xsl:with-param name="label" select="'Filter Cutoff'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'FreezeNoiseFilterSweep'"/>
            <xsl:with-param name="label" select="'Filter Sweep'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>
</xsl:stylesheet>
